#!/usr/bin/env python3
"""Start an isolated, byte-verified npm Studio for hardware integration tests.

Only explicitly selected configuration files are imported. No production DB,
accounts, device identity, relay connection or history is copied. Credentials,
logs and the launch manifest stay below the caller's private directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tarfile
import time
import urllib.request


def private_json(path, value):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, "w") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
    os.chmod(path, 0o600)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--tarball", required=True, type=Path)
    parser.add_argument("--seed-home", required=True, type=Path)
    parser.add_argument("--private-dir", required=True, type=Path)
    parser.add_argument("--node", required=True, type=Path)
    parser.add_argument("--port", default=19038, type=int)
    parser.add_argument("--bridge-port", default=19039, type=int)
    args = parser.parse_args()
    package = args.package.resolve()
    private = args.private_dir.resolve()
    if private.exists() and any(private.iterdir()):
        parser.error("private-dir must be new or empty; existing state is never replaced")
    # Verify every distributed file, not just the main server bundle.
    checked = 0
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
    if version != "0.7.36":
        raise RuntimeError("This firmware contract baseline is official 0.7.36")
    for port in (args.port, args.bridge_port):
        with socket.socket() as probe:
            probe.bind(("0.0.0.0", port))
    private.mkdir(parents=True, mode=0o700, exist_ok=True)
    os.chmod(private, 0o700)
    for directory in ("webui/.ekko/config", "hermes", "coding-global", "workspace", "tmp"):
        (private / directory).mkdir(parents=True, mode=0o700, exist_ok=True)
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
                "serverSha256": hashlib.sha256(entry.read_bytes()).hexdigest(),
                "deviceCode": "cardputer-stock-test-" + secrets.token_hex(12),
                "instanceId": "cardputer-stock-" + secrets.token_hex(12),
                "profile": "default", "workspace": str(private / "workspace"), **credentials}
    private_json(private / "runtime.private.json", manifest)
    for _ in range(120):
        if process.poll() is not None:
            raise RuntimeError("Isolated server exited; inspect its private log")
        try:
            with urllib.request.urlopen(base_url + "/health", timeout=2) as response:
                if response.status == 200:
                    break
        except (OSError, ValueError):
            time.sleep(0.25)
    else:
        process.terminate()
        process.wait(timeout=10)
        raise RuntimeError("Isolated server startup timed out; private log retained, test process stopped")
    def post(path, body, token=None):
        headers = {"Content-Type": "application/json"}
        if token:
            headers["Authorization"] = "Bearer " + token
        request = urllib.request.Request(base_url + path, method="POST", data=json.dumps(body).encode(), headers=headers)
        with urllib.request.urlopen(request, timeout=5) as response:
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
