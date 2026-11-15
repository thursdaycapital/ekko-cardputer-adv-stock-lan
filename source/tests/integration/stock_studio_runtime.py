#!/usr/bin/env python3
"""Start an isolated, verified official Studio for hardware integration tests.

Only explicitly selected configuration files are imported. No production DB,
accounts, device identity, relay connection or history is copied. Credentials,
logs and the launch manifest stay below the caller's private directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import secrets
import socket
import subprocess
import tarfile
import time
import urllib.request

APP_0824_SHA256 = "c04bfec6333a0e70d763ba93ec2fff7bf3f7b6241b6b75c4d5c1d57a571084e0"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        return None


def verify_signed_app(app, package, node):
    app = app.resolve()
    helper = app / "Contents/Frameworks/Ekko Studio Helper.app/Contents/MacOS/Ekko Studio Helper"
    if package != app / "Contents/Resources/webui" or node.resolve() != helper.resolve():
        raise RuntimeError("App mode requires its own packaged webui and Electron Node helper")
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)],
                   check=True, capture_output=True)
    entry = package / "dist/server/index.js"
    if hashlib.sha256(entry.read_bytes()).hexdigest() != APP_0824_SHA256:
        raise RuntimeError("Signed App server entry does not match the reviewed0.8.24 build")
    seal_path = app / "Contents/_CodeSignature/CodeResources"
    seal = plistlib.loads(seal_path.read_bytes())["files2"]
    checked = 0
    for name, row in seal.items():
        actual = app / "Contents" / name
        if not actual.resolve().is_relative_to(app):
            raise RuntimeError("Sealed App resource escapes its bundle")
        if "hash2" in row:
            if hashlib.sha256(actual.read_bytes()).digest() != row["hash2"]:
                raise RuntimeError("Sealed App resource does not match")
            checked += 1
        elif "symlink" in row:
            if not actual.is_symlink() or os.readlink(actual) != row["symlink"]:
                raise RuntimeError("Sealed App link does not match")
        elif "cdhash" not in row:
            raise RuntimeError("Unrecognized sealed App resource")
    return checked


def seed_model_voice(seed_home, private):
    # App mode imports provider/voice settings only; never messenger accounts,
    # gateways, plugins, device identity, DB, sessions, prompts or tool settings.
    import yaml
    daily = yaml.safe_load((seed_home / ".hermes/config.yaml").read_text()) or {}
    selected = {key: daily[key] for key in ("model", "fallback_providers", "custom_providers",
                 "model_catalog", "tts", "stt", "voice") if key in daily}
    target = private / "hermes/config.yaml"
    with os.fdopen(os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "w") as stream:
        yaml.safe_dump(selected, stream, allow_unicode=True, sort_keys=False)
    config = json.loads((seed_home / ".hermes-web-ui/config.json").read_text())
    selected = {key: config[key] for key in ("modelVisibility", "customModels", "modelAliases",
                  "providerLabels", "providerPreferredModels") if key in config}
    selected["gatewayAutoStart"] = {"enabled": False}
    private_json(private / "webui/config.json", selected)
    ekko = json.loads((seed_home / ".hermes-web-ui/.ekko/config/config.json").read_text())
    private_json(private / "webui/.ekko/config/config.json",
                 {key: ekko[key] for key in ("schemaVersion", "model") if key in ekko})


def private_json(path, value):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, "w") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
    os.chmod(path, 0o600)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", required=True, type=Path)
    artifact = parser.add_mutually_exclusive_group(required=True)
    artifact.add_argument("--tarball", type=Path)
    artifact.add_argument("--signed-app", type=Path, help="Exact unmodified signed0.8.24 App")
    parser.add_argument("--seed-home", required=True, type=Path)
    parser.add_argument("--private-dir", required=True, type=Path)
    parser.add_argument("--node", required=True, type=Path)
    parser.add_argument("--port", default=19038, type=int)
    parser.add_argument("--bridge-port", default=19039, type=int)
    args = parser.parse_args()
    os.umask(0o077)
    package = args.package.resolve()
    private = args.private_dir.resolve()
    if private.exists() and any(private.iterdir()):
        parser.error("private-dir must be new or empty; existing state is never replaced")
    # Verify every distributed file, not just the main server bundle.
    checked = 0
    if args.signed_app:
        checked = verify_signed_app(args.signed_app, package, args.node)
    else:
        with tarfile.open(args.tarball, "r:gz") as archive:
            for member in archive.getmembers():
                if not member.isfile():
                    continue
                relative = Path(member.name).relative_to("package")
                actual = (package / relative).resolve()
                if not actual.is_relative_to(package):
                    raise RuntimeError("Package member escapes package root")
                if archive.extractfile(member).read() != actual.read_bytes():
                    raise RuntimeError(f"Package mismatch: {relative}")
                checked += 1
    version = json.loads((package / "package.json").read_text())["version"]
    if version != ("0.8.24" if args.signed_app else "0.7.36"):
        raise RuntimeError("Unexpected verified official package version")
    if args.port != 19038 or args.bridge_port != 19039:
        raise RuntimeError("Only isolated19038/19039 test ports are permitted")
    for port in (args.port, args.bridge_port):
        with socket.socket() as probe:
            probe.bind(("0.0.0.0", port))
    private.mkdir(parents=True, mode=0o700, exist_ok=True)
    os.chmod(private, 0o700)
    for directory in ("webui/.ekko/config", "hermes", "coding-global", "workspace", "tmp"):
        (private / directory).mkdir(parents=True, mode=0o700, exist_ok=True)
    if args.signed_app:
        seed_model_voice(args.seed_home, private)
    else:
        for relative in ("hermes/config.yaml", "webui/config.json", "webui/.ekko/config/config.json"):
            target = private / relative
            descriptor = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(descriptor, "wb") as stream:
                stream.write((args.seed_home / relative).read_bytes())
    environment = os.environ.copy()
    environment.update({
        "NODE_ENV": "production", "PORT": str(args.port), "BIND_HOST": "0.0.0.0",
        "HERMES_WEB_UI_HOME": str(private / "webui"),
        "HERMES_HOME": str(private / "hermes"),
        "HERMES_CODING_AGENT_GLOBAL_HOME": str(private / "coding-global"),
        "HERMES_AGENT_BRIDGE_ENDPOINT": f"tcp://127.0.0.1:{args.bridge_port}",
        "HERMES_AGENT_BRIDGE_WORKER_TRANSPORT": "tcp",
        "HERMES_AGENT_BRIDGE_WORKER_PORT_BASE": str(args.bridge_port + 10),
        "HERMES_AGENT_BRIDGE_STOP_ON_SHUTDOWN": "1", "TMPDIR": str(private / "tmp"),
    })
    # The parent's API/model/voice keys are not an accidental second provider.
    for key in list(environment):
        if key.endswith(("API_KEY", "ACCESS_TOKEN")) or key.startswith(("VOLC_TTS_", "OPENAI_", "ANTHROPIC_")):
            environment.pop(key, None)
    for key in ("AUTH_JWT_SECRET", "HERMES_WEB_UI_AUTH_JWT_EXPIRES_IN", "HERMES_WEB_UI_TEST_DB_DIR",
                "HERMES_WEBUI_STATE_DIR", "NODE_OPTIONS", "NODE_PATH"):
        environment.pop(key, None)
    environment.update({"HERMES_WEB_UI_DISABLE_UPDATE_CHECK": "1", "PYTHONDONTWRITEBYTECODE": "1",
                        "PYTHONPYCACHEPREFIX": str(private / "tmp/pycache")})
    if args.signed_app:
        environment["ELECTRON_RUN_AS_NODE"] = "1"
        environment["HERMES_DESKTOP"] = "false"
    log_path = private / "server.private.log"
    descriptor = os.open(log_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    entry = package / "dist/server/index.js"
    with os.fdopen(descriptor, "wb") as stream:
        process = subprocess.Popen([str(args.node), str(entry)], cwd=private / "workspace",
                                   env=environment, stdout=stream, stderr=subprocess.STDOUT,
                                   start_new_session=True)
    base_url = f"http://127.0.0.1:{args.port}"
    credentials = {"username": "admin", "password": secrets.token_urlsafe(24)}
    manifest = {"pid": process.pid, "baseUrl": base_url, "port": args.port,
                "bridgePort": args.bridge_port, "node": str(args.node.resolve()), "entry": str(entry),
                "version": version, "verifiedPackageFiles": checked,
                "artifactMode": "signed-app" if args.signed_app else "npm-tarball",
                "strictAppSignatureVerified": bool(args.signed_app),
                "serverSha256": hashlib.sha256(entry.read_bytes()).hexdigest(),
                "deviceCode": "cardputer-stock-test-" + secrets.token_hex(12),
                "instanceId": "cardputer-stock-" + secrets.token_hex(12),
                "profile": "default", "workspace": str(private / "workspace"), **credentials}
    private_json(private / "runtime.private.json", manifest)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    startup_deadline = time.monotonic() + 180
    while time.monotonic() < startup_deadline:
        if process.poll() is not None:
            raise RuntimeError("Isolated server exited; inspect its private log")
        try:
            request = urllib.request.Request(base_url + "/health?_bootstrap=" + secrets.token_hex(12),
                                            headers={"Cache-Control": "no-cache, no-store"})
            with opener.open(request, timeout=2) as response:
                if response.status == 200:
                    break
        except (OSError, ValueError):
            time.sleep(1)
    else:
        process.terminate()
        process.wait(timeout=10)
        raise RuntimeError("Isolated server startup timed out; private log retained, test process stopped")
    def post(path, body, token=None):
        headers = {"Content-Type": "application/json"}
        if token:
            headers["Authorization"] = "Bearer " + token
        request = urllib.request.Request(base_url + path, method="POST", data=json.dumps(body).encode(), headers=headers)
        with opener.open(request, timeout=5) as response:
            return json.load(response)
    # Stock initializes its first account only with these published defaults.
    # Authenticate once, then immediately replace its password; do not retry
    # authentication failures until the login limiter locks the fresh instance.
    try:
        bootstrap = post("/api/auth/login", {"username": "admin", "password": "123456"})
        post("/api/auth/change-password", {"currentPassword": "123456", "newPassword": credentials["password"]}, bootstrap["token"])
        login = post("/api/auth/login", credentials)
    except Exception:
        # Never retain an externally listening fresh instance with its default
        # password after setup failed. Only our Popen child is terminated.
        process.terminate()
        process.wait(timeout=10)
        raise
    manifest["userJwt"] = login["token"]
    private_json(private / "runtime.private.json", manifest)
    print(json.dumps({"ready": True, "pid": process.pid, "port": args.port,
                      "verifiedPackageFiles": checked, "version": version,
                      "privateManifest": str(private / "runtime.private.json")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
