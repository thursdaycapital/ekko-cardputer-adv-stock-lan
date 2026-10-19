#!/usr/bin/env python3
"""Official isolated Studio/M5 auth recovery. Default is read-only preflight.

--execute tests signature invalidation/restoration ONLY on verified port19038.
Actual45-second expiry remains a separately documented, unimplemented stage.
--restore-only recovers the private journal. No Studio/firmware is modified.
Tokens, process environments and response bodies must never go to stdout.
"""
import argparse
import base64
import ctypes
import datetime
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import secrets
import signal
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

OFFICIAL_SHA256 = "2698df3d144d32b435d8b700dc53c0b79879e9041faed557f883d7a9dac93f0a"
ALLOWED_PORT = 19038
STARTUP_SECONDS = 180


class Failure(RuntimeError):
    """Only locally authored, non-sensitive diagnostic messages belong here."""


def require(condition, message):
    if not condition:
        raise Failure(message)


def private_json(path, value):
    temporary = path.with_name(path.name + ".new")
    created = False
    try:
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        created = True
        with os.fdopen(fd, "w") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if created and temporary.exists():
            temporary.unlink()


def command(arguments):
    return subprocess.run(arguments, capture_output=True, timeout=8, check=False)


def listeners(port):
    result = command(["lsof", "-nP", "-iTCP:" + str(port), "-sTCP:LISTEN"])
    return sorted({int(line.split()[1]) for line in result.stdout.decode().splitlines()[1:]})


def process_stamp(pid):
    result = command(["ps", "-p", str(pid), "-o", "stat=,lstart="])
    parts = result.stdout.decode().strip().split(None, 1)
    return tuple(parts) if result.returncode == 0 and len(parts) == 2 else None


def running_stamp(stamp):
    # kill(pid,0) remains true for a zombie. It is not an exit-wait primitive.
    return bool(stamp and not stamp[0].startswith(("Z", "X")))


def parse_mac_procargs(data):
    require(len(data) >= 4, "Process argument record is incomplete")
    argc = struct.unpack_from("=i", data)[0]
    require(0 < argc < 10000, "Process argument count is invalid")
    offset = data.index(b"\0", 4) + 1  # executable path, then padding
    while offset < len(data) and data[offset] == 0:
        offset += 1
    argv = []
    for _ in range(argc):
        end = data.index(b"\0", offset)
        argv.append(os.fsdecode(data[offset:end]))
        offset = end + 1
    env = {}
    for item in data[offset:].split(b"\0"):
        if b"=" in item:
            key, value = item.split(b"=", 1)
            env[os.fsdecode(key)] = os.fsdecode(value)
    return argv, env


def process_arguments(pid):
    if sys.platform == "darwin":
        libc = ctypes.CDLL(None, use_errno=True)
        libc.sysctl.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_uint, ctypes.c_void_p,
                                ctypes.POINTER(ctypes.c_size_t), ctypes.c_void_p, ctypes.c_size_t]
        mib = (ctypes.c_int * 3)(1, 49, pid)  # CTL_KERN / KERN_PROCARGS2
        length = ctypes.c_size_t()
        require(libc.sysctl(mib, 3, None, ctypes.byref(length), None, 0) == 0,
                "Cannot read isolated process arguments")
        buffer = ctypes.create_string_buffer(length.value)
        require(libc.sysctl(mib, 3, buffer, ctypes.byref(length), None, 0) == 0,
                "Cannot read isolated process environment")
        return parse_mac_procargs(buffer.raw[:length.value])
    require(sys.platform.startswith("linux"), "Process verification requires macOS or Linux")
    root = Path("/proc") / str(pid)
    argv = [os.fsdecode(value) for value in (root / "cmdline").read_bytes().split(b"\0") if value]
    env = {}
    for item in (root / "environ").read_bytes().split(b"\0"):
        if b"=" in item:
            key, value = item.split(b"=", 1)
            env[os.fsdecode(key)] = os.fsdecode(value)
    return argv, env


def jwt_lifetime(token):
    # Read timing only, after the same token has passed official /auth/me.
    part = token.split(".")[1]
    value = json.loads(base64.urlsafe_b64decode(part + "=" * (-len(part) % 4)))
    return int(value["exp"]) - int(value["iat"])


def validate_manifest(manifest, home):
    url = urllib.parse.urlsplit(manifest["baseUrl"])
    require(url.scheme == "http" and url.hostname in ("127.0.0.1", "localhost") and
            url.port == ALLOWED_PORT and manifest["port"] == ALLOWED_PORT and
            not url.username and not url.password and not url.query and not url.fragment and
            url.path in ("", "/"), "Only the isolated loopback19038 instance is permitted")
    require(manifest.get("version") == "0.7.36" and manifest["bridgePort"] == 19039,
            "Unexpected official version or isolated bridge port")
    entry = Path(manifest["entry"]).resolve()
    require(hashlib.sha256(entry.read_bytes()).hexdigest() == OFFICIAL_SHA256 and
            manifest["serverSha256"] == OFFICIAL_SHA256, "Official server hash mismatch")
    require(Path(manifest["workspace"]).resolve() == (home / "workspace").resolve(),
            "Workspace must remain inside the original private home")


def validate_environment(env, home):
    for key, relative in (("HERMES_WEB_UI_HOME", "webui"), ("HERMES_HOME", "hermes"),
                          ("HERMES_CODING_AGENT_GLOBAL_HOME", "coding-global"), ("TMPDIR", "tmp")):
        require(Path(env.get(key, "")).resolve() == (home / relative).resolve(),
                "Isolated environment home mismatch")
    if env.get("HERMES_WEBUI_STATE_DIR"):
        require(Path(env["HERMES_WEBUI_STATE_DIR"]).resolve() == (home / "webui").resolve(),
                "Compatibility state directory mismatch")
    require(env.get("PORT") == str(ALLOWED_PORT) and
            env.get("HERMES_AGENT_BRIDGE_ENDPOINT") == "tcp://127.0.0.1:19039" and
            env.get("HERMES_AGENT_BRIDGE_WORKER_PORT_BASE") == "19049" and
            env.get("HERMES_AGENT_BRIDGE_STOP_ON_SHUTDOWN") == "1",
            "Isolated port or bridge ownership mismatch")


def bundle_capabilities(entry):
    data = entry.read_bytes()
    # This exact distributed bundle was independently audited for the ordinary
    # MCU /chat-run JWT path. A different build requires a fresh contract audit;
    # source-only logger statements must not be treated as binary capabilities.
    known = hashlib.sha256(data).hexdigest() == OFFICIAL_SHA256
    markers = all(marker in data for marker in (b"[mcu-login] completed", b"[mcu-login] phase completed"))
    return {"nativeLoginLogMarkers": markers, "ordinaryChatJwtContractVerified": known,
            "globalAgentHasSeparateProcessLocalAgentToken": known}


class NativeLoginAudit:
    """Consume only safe official log fields; never copy complete log lines."""
    def __init__(self, path, available=True):
        self.path = path
        self.available = available
        if available:
            stat = path.stat()
            self.inode, self.offset = stat.st_ino, stat.st_size
        self.pending = b""
        self.attempts = self.completed = self.refused = 0
        self.last_completed_seen = None

    def poll(self):
        if not self.available:
            return {"nativeLoginCountsAvailable": False, "nativeLoginAudit": "not_available",
                    "nativeLoginAttempts": None, "nativeLoginCompleted": None, "nativeLoginRefused": None}
        stat = self.path.stat()
        require(stat.st_ino == self.inode and stat.st_size >= self.offset,
                "Official log rotated or truncated; native-login attribution is inconclusive")
        with self.path.open("rb") as stream:
            stream.seek(self.offset)
            self.pending += stream.read()
            self.offset = stream.tell()
        lines = self.pending.split(b"\n")
        self.pending = lines.pop()
        for line in lines:
            try:
                row = json.loads(line)
            except (ValueError, UnicodeError):
                continue
            if row.get("msg") == "[mcu-login] phase completed" and row.get("phase") == "credentials":
                self.attempts += 1
                self.refused += int(row.get("status") in (401, 403))
            if row.get("msg") == "[mcu-login] completed" and row.get("status") == 200 and row.get("remote") is False:
                self.completed += 1
                self.last_completed_seen = time.monotonic()
        return {"nativeLoginCountsAvailable": True, "nativeLoginAudit": "official-log-markers",
                "nativeLoginAttempts": self.attempts, "nativeLoginCompleted": self.completed,
                "nativeLoginRefused": self.refused}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        return None


class Recovery:
    def __init__(self, manifest_path, device_state, nvs_path, report_path=None):
        self.path = manifest_path.resolve()
        self.home = self.path.parent
        require(self.path.stat().st_mode & 0o077 == 0 and self.home.stat().st_mode & 0o077 == 0,
                "Runtime manifest and home must be private")
        self.original = json.loads(self.path.read_text())
        validate_manifest(self.original, self.home)
        self.capabilities = bundle_capabilities(Path(self.original["entry"]))
        snapshot = json.loads(device_state.read_text())
        address = ipaddress.ip_address(snapshot.get("ip") or snapshot.get("health", {}).get("ip", ""))
        require(address.is_private and not address.is_loopback and address.version == 4,
                "Device snapshot must identify a private IPv4 M5 address")
        self.device_url = "http://" + str(address) + "/health"
        self.device_code = json.loads(nvs_path.read_text())["mcu"]["device_code"]
        self.pid = int(self.original["pid"])
        self.owned = {}
        self.host_jwt = self.original["userJwt"]
        self.journal = self.home / "physical-auth-recovery.private.json"
        self.audit_path = self.home / "webui/logs/server.log"
        self.token_path = self.home / "webui/.token"
        self.original_environment = None
        self.report_path = report_path.resolve() if report_path else None
        if self.report_path:
            require(self.report_path.parent == (self.home.parent.parent / "evidence").resolve() and
                    self.report_path.name.startswith("physical-auth-") and self.report_path.suffix == ".json",
                    "Evidence output must be a separate physical-auth JSON under this work directory's evidence")
        self.allow_management_login = False
        self.birth = None
        self.last_uptime = None
        self.agent = None
        self.verified_auth_transition = False
        self.report = {"mode": "preflight", "productionPort8748Touched": False,
                       "physicalAuthTestExecuted": False, "chatRequests": 0, "paidVoiceCalls": 0,
                       "m5ManualLoginRequests": 0, "hostMcuLoginRequests": 0,
                       "timeExpiryStageExecuted": False,
                       "healthReauthCountersAvailable": False, "stages": [], "findings": []}

    def http(self, path, token=None, body=None, device=False, timeout=5):
        headers = {"x-hermes-profile": self.original.get("profile", "default")}
        if token:
            headers["Authorization"] = "Bearer " + token
        if body is not None:
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(self.device_url if device else self.original["baseUrl"] + path,
                                        data=None if body is None else json.dumps(body).encode(), headers=headers)
        try:
            with urllib.request.build_opener(NoRedirect()).open(request, timeout=timeout) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as error:
            error.close()
            return error.code, b""

    def verify_process(self, require_listener=True):
        owners = listeners(ALLOWED_PORT)
        require(owners == [self.pid] if require_listener else owners in ([], [self.pid]),
                "An unexpected process owns19038")
        require(self.pid not in listeners(8748), "Refusing to signal a process serving production8748")
        stamp = process_stamp(self.pid)
        require(running_stamp(stamp), "Isolated process is not running")
        argv, env = process_arguments(self.pid)
        require(len(argv) == 2 and Path(argv[0]).resolve() == Path(self.original["node"]).resolve() and
                Path(argv[1]).resolve() == Path(self.original["entry"]).resolve(),
                "Isolated process executable or entry mismatch")
        validate_manifest(self.original, self.home)
        validate_environment(env, self.home)
        if self.birth:
            require(stamp[1] == self.birth, "Isolated PID was reused")
        return stamp, env

    def device_health(self):
        status, body = self.http("", device=True)
        require(status == 200, "M5 health is unavailable")
        value = json.loads(body)
        uptime = int(value["uptime_ms"])
        if self.last_uptime is not None:
            require(uptime >= self.last_uptime, "M5 rebooted during the authentication-only test")
        self.last_uptime = uptime
        if self.agent is not None:
            require(value.get("agentId") == self.agent, "M5 Agent changed during the authentication-only test")
        connection = value.get("connection", {})
        return value, all(connection.get(key) is True for key in
                          ("namespace_ready", "management_online", "saved_binding_matches")) and \
            connection.get("http_port") == ALLOWED_PORT

    def presence(self):
        status, body = self.http("/api/mcu-devices", self.host_jwt)
        if status == 401:
            require(self.allow_management_login, "Preflight JWT expired; no login POST was attempted")
            self.management_login()
            status, body = self.http("/api/mcu-devices", self.host_jwt)
        require(status == 200, "Official native device inventory unavailable")
        rows = json.loads(body).get("devices", [])
        matched = [row for row in rows if row.get("device_code") == self.device_code]
        require(len(matched) == 1, "Real M5 native registration is not unique")
        require(sum(row.get("lan_connected") is True for row in rows) <= 1,
                "Other active MCU clients prevent exclusive native-login attribution")
        return matched[0].get("lan_connected") is True

    def management_login(self):
        status, body = self.http("/api/auth/login", body={"username": self.original["username"],
                                                       "password": self.original["password"]})
        require(status == 200, "Private management authentication failed")
        self.host_jwt = json.loads(body)["token"]
        require(self.http("/api/auth/me", self.host_jwt)[0] == 200,
                "New management JWT was not accepted")
        return jwt_lifetime(self.host_jwt)

    def verify_bridge(self):
        for pid in listeners(19039):
            argv, env = process_arguments(pid)
            script = Path(self.original["entry"]).parent / "agent-bridge/python/hermes_bridge.py"
            require(any(Path(arg).resolve() == script.resolve() for arg in argv if not arg.startswith("-")) and
                    Path(env.get("HERMES_HOME", "")).resolve() == (self.home / "hermes").resolve(),
                    "Unknown bridge19039 occupant; it will not be stopped or reused")

    def native_audit(self):
        return NativeLoginAudit(self.audit_path, available=self.capabilities["nativeLoginLogMarkers"])

    def new_server_has_m5_tcp(self):
        address = urllib.parse.urlsplit(self.device_url).hostname
        rows = command(["lsof", "-nP", "-a", "-p", str(self.pid), "-iTCP"]).stdout.decode().splitlines()[1:]
        for row in rows:
            fields = row.split()
            if len(fields) >= 10 and fields[-1] == "(ESTABLISHED)" and \
                    (":" + str(ALLOWED_PORT) + "->" + address + ":") in fields[8]:
                return True
        return False

    def preflight(self, normal=True):
        stamp, env = self.verify_process()
        if normal:
            require(not env.get("AUTH_JWT_SECRET") and not env.get("HERMES_WEB_UI_AUTH_JWT_EXPIRES_IN"),
                    "Preflight requires the original default secret and default lifetime")
        self.birth = stamp[1]
        self.original_environment = env
        require(self.http("/health")[0] == 200, "Official Studio health failed")
        require(self.http("/api/auth/me", self.original["userJwt"])[0] == 200,
                "Saved original JWT is not accepted")
        health, ready = self.device_health()
        self.agent = health.get("agentId")
        require(ready and self.presence(), "M5 must already be bound and online19038")
        require(not health.get("audio", {}).get("busy") and not health.get("chat", {}).get("sending") and
                not health.get("mcu", {}).get("audio_playing"), "M5 must be idle before auth testing")
        require(self.token_path.is_file(), "Private token file missing")
        if self.capabilities["nativeLoginLogMarkers"]:
            require(self.audit_path.is_file(), "Compiled native audit markers exist, but the private log is unavailable")
        require(self.capabilities["nativeLoginLogMarkers"] or self.capabilities["ordinaryChatJwtContractVerified"],
                "Neither native audit nor the pinned ordinary-JWT observation contract is available")
        self.verify_bridge()
        self.report.update({"preflightPassed": True, "officialVersion": "0.7.36",
                            "serverSha256": OFFICIAL_SHA256, "isolatedPid": self.pid,
                            "startupBudgetSeconds": STARTUP_SECONDS, "originalJwtHttp": 200,
                            "m5Target19038": True, "m5DualNamespaceReady": True,
                            "savedBindingMatches": True, "nativePresenceOnline": True,
                            "nativeLoginAuditAvailable": self.capabilities["nativeLoginLogMarkers"],
                            "nativeLoginCountsAvailable": self.capabilities["nativeLoginLogMarkers"],
                            "nativeLoginAttempts": None, "nativeLoginCompleted": None,
                            "nativeLoginRefused": None,
                            "recoveryEvidenceMode": "official-native-log" if self.capabilities["nativeLoginLogMarkers"]
                                else "derived-new-process-chat-jwt-handshake",
                            "nativeLoginAudit": "available" if self.capabilities["nativeLoginLogMarkers"] else "not_available",
                            "manualLoginExclusion": "operator-controlled; not instrumented",
                            "hostMcuLoginCountScope": "requests made by this harness only"})
        return self.report

    def save_state(self, **updates):
        self.state.update(updates)
        self.state["sanitizedProgress"] = self.report
        private_json(self.journal, self.state)
        if self.report_path:
            private_json(self.report_path, self.report)

    def stop(self):
        owned = self.owned.get(self.pid)
        if owned and owned.poll() is not None:
            owned.wait()  # reap our child; do not wait on kill(pid,0)
            return
        stamp = process_stamp(self.pid)
        if not running_stamp(stamp):
            require(not listeners(ALLOWED_PORT), "An unexpected process owns19038")
            return
        self.verify_process(require_listener=False)  # also permits our verified pre-listen startup
        os.kill(self.pid, signal.SIGTERM)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if owned and owned.poll() is not None:
                owned.wait()
                break
            if not running_stamp(process_stamp(self.pid)):
                break
            time.sleep(0.2)
        else:
            self.verify_process(require_listener=False)
            os.kill(self.pid, signal.SIGKILL)
            if owned:
                owned.wait(timeout=5)
        if owned:
            owned.wait(timeout=5)
        deadline = time.monotonic() + 5
        while listeners(ALLOWED_PORT) and time.monotonic() < deadline:
            time.sleep(0.1)
        require(not listeners(ALLOWED_PORT), "Isolated listener did not release19038")

    def start(self, environment):
        require(not listeners(ALLOWED_PORT), "Refusing a duplicate19038 instance")
        validate_environment(environment, self.home)
        self.verify_bridge()  # Read-only ownership check; never kill an unknown listener.
        log = self.home / "server.private.log"
        with log.open("ab") as stream:
            os.chmod(log, 0o600)
            child = subprocess.Popen([self.original["node"], self.original["entry"]],
                                     cwd=self.original["workspace"], env=environment,
                                     stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        self.pid, self.birth = child.pid, None
        self.owned[child.pid] = child
        self.save_state(currentPid=self.pid, currentBirth=None)
        private_json(self.path, {**self.original, "pid": self.pid})
        deadline = time.monotonic() + STARTUP_SECONDS
        while time.monotonic() < deadline:
            require(child.poll() is None, "Isolated server exited during startup")
            try:
                if self.http("/health", timeout=2)[0] == 200:
                    stamp, _ = self.verify_process()
                    self.birth = stamp[1]
                    self.save_state(currentBirth=self.birth)
                    return
            except OSError:
                pass
            time.sleep(1)
        raise Failure("Official startup exceeded180 seconds; private log retained")

    def wait_m5(self, audit, require_native=True):
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            counts = audit.poll()
            try:
                _, ready = self.device_health()
                logged = counts["nativeLoginCountsAvailable"] and counts["nativeLoginCompleted"] > 0
                derived = not counts["nativeLoginCountsAvailable"] and \
                    self.capabilities["ordinaryChatJwtContractVerified"] and self.verified_auth_transition
                if ready and (logged or derived or not require_native) and self.presence() and self.new_server_has_m5_tcp():
                    return {**counts, "dualNamespaceReady": True, "savedBindingMatches": True,
                            "target19038": True, "nativePresenceOnline": True,
                            "newServerTcpToM5": True, "m5UptimeContinuous": True, "m5AgentUnchanged": True,
                            "automaticReauthenticationObserved": bool(require_native and (logged or derived)),
                            "recoveryEvidenceMode": "official-native-log" if logged else
                                "derived-new-process-chat-jwt-handshake" if derived else "connection-restoration-only"}
            except OSError:
                pass
            time.sleep(2)
        raise Failure("M5 automatic authentication recovery did not complete")

    def restore(self):
        self.allow_management_login = True
        # Audit loss must never prevent restoring the original signing process.
        try:
            audit = self.native_audit()
        except (OSError, Failure):
            audit = None
        self.stop()
        self.start(self.original_environment)
        _, env = self.verify_process()
        require(not env.get("AUTH_JWT_SECRET") and not env.get("HERMES_WEB_UI_AUTH_JWT_EXPIRES_IN") and
                env == self.original_environment, "Original process environment was not restored")
        self.report["restoration"] = {"normalEnvironmentRestored": True, "originalEnvironmentRestored": True,
                                      "temporarySecretRemoved": True, "authVerified": False,
                                      "deviceRecoveryVerified": False}
        self.save_state(normalRestored=True, phase="normal-environment-restored")
        require(self.http("/api/auth/me", self.original["userJwt"])[0] == 200,
                "Original JWT failed after restoring the original signing environment")
        lifetime = self.management_login()
        require(lifetime == 30 * 24 * 3600, "Normal JWT lifetime was not restored to30 days")
        require(hashlib.sha256(self.token_path.read_bytes()).hexdigest() == self.state["tokenHash"],
                "Persisted signing-token file changed unexpectedly")
        private_json(self.path, {**self.original, "pid": self.pid, "userJwt": self.host_jwt})
        self.report["restoration"].update(originalJwtHttp=200, defaultLifetimeSeconds=lifetime,
                                           authVerified=True, persistedTokenUnchanged=True, privateManifestUpdated=True)
        self.save_state(phase="normal-auth-verified")
        try:
            require(audit is not None, "Normal environment restored, but native login audit is unavailable")
            self.verified_auth_transition = self.state.get("temporaryBindingObserved", False)
            recovered = self.wait_m5(audit, require_native=self.state.get("temporaryBindingObserved", False))
        except (OSError, Failure):
            self.save_state(phase="normal-restored-device-audit-inconclusive")
            raise
        self.report["restoration"].update(**recovered, deviceRecoveryVerified=True)
        self.save_state(normalRestored=True, phase="restored")

    def execute(self):
        self.preflight()
        if self.journal.exists():
            previous = json.loads(self.journal.read_text())
            require(previous.get("normalRestored") is True, "Unrestored private journal exists; use --restore-only")
        self.state = {"originalManifest": self.original, "originalEnvironment": self.original_environment,
                      "currentPid": self.pid, "currentBirth": self.birth,
                      "baselineAgent": self.agent, "baselineDeviceUptime": self.last_uptime,
                      "tokenHash": hashlib.sha256(self.token_path.read_bytes()).hexdigest(),
                      "normalRestored": False, "temporaryBindingObserved": False, "phase": "prepared"}
        self.save_state()
        self.allow_management_login = True
        self.report.update(mode="execute", physicalAuthTestExecuted=True)
        temporary = dict(self.original_environment)
        temporary["AUTH_JWT_SECRET"] = secrets.token_urlsafe(48)
        try:
            self.save_state(phase="signature-invalidation-starting")
            audit = self.native_audit()
            self.stop(); self.start(temporary)
            require(self.http("/api/auth/me", self.original["userJwt"])[0] == 401,
                    "Original signature was not invalidated")
            self.verified_auth_transition = True
            require(self.management_login() == 30 * 24 * 3600, "Default lifetime changed unexpectedly")
            first = self.wait_m5(audit)
            self.report["stages"].append({"name": "signature invalidation", "oldJwtHttp": 401, **first})
            self.save_state(temporaryBindingObserved=True, phase="signature-recovered")
            self.report["authenticationChecksPassed"] = True
        finally:
            try:
                self.save_state(phase="restoring-normal-environment")
            except OSError:
                self.report["journalUpdateFailedBeforeRestoration"] = True
            self.restore()
        self.report["passed"] = True
        return self.report

    def restore_only(self):
        require(self.journal.is_file(), "No private recovery journal exists")
        self.state = json.loads(self.journal.read_text())
        self.original = self.state["originalManifest"]
        validate_manifest(self.original, self.home)
        self.original_environment = self.state["originalEnvironment"]
        validate_environment(self.original_environment, self.home)
        require(not self.original_environment.get("AUTH_JWT_SECRET") and
                not self.original_environment.get("HERMES_WEB_UI_AUTH_JWT_EXPIRES_IN"),
                "Journal does not contain a default-auth baseline")
        self.pid, self.birth = self.state["currentPid"], self.state.get("currentBirth")
        self.agent, self.last_uptime = self.state.get("baselineAgent"), self.state.get("baselineDeviceUptime")
        self.report["mode"] = "restore-only"
        self.restore()
        self.report["passed"] = True
        return self.report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--device-state", type=Path, required=True, help="Private health snapshot; only its IP is used")
    parser.add_argument("--nvs-values", type=Path, required=True, help="Private identity snapshot; never printed")
    parser.add_argument("--report", type=Path, help="Optional sanitized evidence output")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--execute", action="store_true")
    mode.add_argument("--restore-only", action="store_true")
    args = parser.parse_args()
    run = None
    try:
        run = Recovery(args.manifest, args.device_state, args.nvs_values, args.report)
        if args.execute or args.restore_only:
            # A permanent empty lock is intentional; it contains no credentials.
            import fcntl
            lock = os.open(run.home / "physical-auth-recovery.lock", os.O_CREAT | os.O_RDWR, 0o600)
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                def interrupted(signum, frame):
                    raise KeyboardInterrupt()
                old_term = signal.signal(signal.SIGTERM, interrupted)
                try:
                    result = run.execute() if args.execute else run.restore_only()
                finally:
                    signal.signal(signal.SIGTERM, old_term)
            finally:
                os.close(lock)
        else:
            result = run.preflight()
            result["passed"] = True
    except BaseException as error:
        result = run.report if run else {"mode": "preflight"}
        result.update(passed=False, failureType=type(error).__name__)
        if isinstance(error, Failure):
            result["failure"] = str(error)  # authored messages only
    result["checkedAtUtc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    result["limitations"] = ["Health has no native reauth counters. Counts are null when markers are absent from the actual distributed bundle.",
                             "Native-login attribution requires exclusive M5 use of the isolated instance.",
                             "Observed renewal is an inference from a controlled signing-key transition, new-server ordinary /chat-run ACK, live TCP/presence and no host MCU login or manual device login.",
                             "/global-agent additionally accepts a separate process-local agent token; presence alone is insufficient to prove a new user JWT.",
                             "M5 JWT contents are not read. The actual45-second time-expiry stage is not implemented or executed by this entry point.",
                             "An idle authenticated WebSocket need not disconnect at JWT exp; expiry is tested by a new handshake.",
                             "Authentication recovery does not independently verify a model response or physical audio."]
    if args.report and run:
        private_json(run.report_path, result)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get("passed") else 1


if __name__ == "__main__":
    sys.exit(main())
