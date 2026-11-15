"""Offline safety regressions. No listener, device, model or voice requests."""
import hashlib
import base64
import json
from pathlib import Path
import signal
import struct
import tempfile
import unittest
from unittest.mock import Mock, patch

import physical_auth_recovery as auth


class RecoverySafetyTests(unittest.TestCase):
    def test_mac_arguments_preserve_spaces_and_equals_without_printing_environment(self):
        argv = ["/private/node", "/private/project with spaces/server.js"]
        raw = struct.pack("=i", 2) + b"/private/node\0\0\0"
        raw += b"\0".join(value.encode() for value in argv) + b"\0"
        raw += b"PATH=/bin\0EXAMPLE=contains spaces=and equals\0\0"
        actual, environment = auth.parse_mac_procargs(raw)
        self.assertEqual(actual, argv)
        self.assertEqual(environment["EXAMPLE"], "contains spaces=and equals")

    def test_manifest_hard_rejects_production_and_nonloopback(self):
        for url, port in [("http://127.0.0.1:8748", 8748), ("http://example.test:19038", 19038),
                          ("http://127.0.0.1:19038/?secret=x", 19038)]:
            with self.subTest(port=port), self.assertRaises(auth.Failure):
                auth.validate_manifest({"baseUrl": url, "port": port}, Path("/private/test"))

    def test_official_hash_and_workspace_are_not_taken_on_trust(self):
        with tempfile.TemporaryDirectory() as folder:
            home = Path(folder)
            entry = home / "index.js"
            entry.write_bytes(b"offline synthetic entry")
            digest = hashlib.sha256(entry.read_bytes()).hexdigest()
            manifest = {"baseUrl": "http://127.0.0.1:19038", "port": 19038, "bridgePort": 19039,
                        "version": "0.7.36", "entry": str(entry), "serverSha256": digest,
                        "workspace": str(home / "workspace")}
            with patch.object(auth, "OFFICIAL_SHA256", digest):
                auth.validate_manifest(manifest, home)
                manifest["workspace"] = str(home / "elsewhere")
                with self.assertRaises(auth.Failure):
                    auth.validate_manifest(manifest, home)
            with self.assertRaises(auth.Failure):
                auth.validate_manifest(manifest, home)

    def test_environment_cannot_reuse_production_home_or_bridge(self):
        home = Path("/private/isolated")
        environment = {"HERMES_WEB_UI_HOME": str(home / "webui"), "HERMES_HOME": str(home / "hermes"),
                       "HERMES_CODING_AGENT_GLOBAL_HOME": str(home / "coding-global"), "TMPDIR": str(home / "tmp"),
                       "PORT": "19038", "HERMES_AGENT_BRIDGE_ENDPOINT": "tcp://127.0.0.1:19039",
                       "HERMES_AGENT_BRIDGE_WORKER_PORT_BASE": "19049", "HERMES_AGENT_BRIDGE_STOP_ON_SHUTDOWN": "1"}
        auth.validate_environment(environment, home)
        for key, value in [("PORT", "8748"), ("HERMES_HOME", "/private/production"),
                           ("HERMES_AGENT_BRIDGE_ENDPOINT", "tcp://127.0.0.1:49165")]:
            with self.subTest(key=key), self.assertRaises(auth.Failure):
                auth.validate_environment({**environment, key: value}, home)

    def test_current_app_entry_is_pinned_by_version_and_hash_not_manifest_claim(self):
        with tempfile.TemporaryDirectory() as folder:
            home = Path(folder)
            entry = home / "index.js"
            entry.write_bytes(b"offline synthetic current App entry")
            digest = hashlib.sha256(entry.read_bytes()).hexdigest()
            manifest = {"baseUrl": "http://127.0.0.1:19038", "port": 19038, "bridgePort": 19039,
                        "version": "0.8.24", "entry": str(entry), "serverSha256": digest,
                        "workspace": str(home / "workspace")}
            with patch.object(auth, "APP_0824_SHA256", digest):
                auth.validate_manifest(manifest, home)
                self.assertTrue(auth.bundle_capabilities(entry)["ordinaryChatJwtContractVerified"])
                manifest["version"] = "0.8.25"
                with self.assertRaises(auth.Failure):
                    auth.validate_manifest(manifest, home)
            manifest["version"] = "0.8.24"
            with self.assertRaises(auth.Failure):
                auth.validate_manifest(manifest, home)

    def test_fresh_machine_identity_must_match_before_recovery_is_attributed(self):
        run = auth.Recovery.__new__(auth.Recovery)
        expected = ("synthetic-machine", "desktop", 19038)
        run.machine_identity = expected
        run.http = Mock(return_value=(200, json.dumps({"device_id": expected[0],
            "endpoint_kind": expected[1], "http_port": expected[2]}).encode()))
        self.assertEqual(run.verify_machine_identity(), expected)
        for field, changed in (("device_id", "other-machine"), ("endpoint_kind", "other-kind"),
                               ("http_port", 8748)):
            row = {"device_id": expected[0], "endpoint_kind": expected[1], "http_port": expected[2]}
            row[field] = changed
            run.http.return_value = (200, json.dumps(row).encode())
            with self.subTest(field=field), self.assertRaises(auth.Failure):
                run.verify_machine_identity()

    def test_private_writes_are_atomic_and_do_not_delete_an_existing_temporary(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "state.private.json"
            auth.private_json(path, {"fixture": True})
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            temporary = path.with_name(path.name + ".new")
            temporary.write_text("existing unrelated file")
            with self.assertRaises(FileExistsError):
                auth.private_json(path, {"fixture": False})
            self.assertEqual(temporary.read_text(), "existing unrelated file")
            self.assertTrue(json.loads(path.read_text())["fixture"])

    def test_native_audit_uses_safe_fields_and_handles_partial_lines(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "server.log"
            path.write_text("")
            audit = auth.NativeLoginAudit(path)
            attempt = {"msg": "[mcu-login] phase completed", "phase": "credentials", "status": 200}
            success = {"msg": "[mcu-login] completed", "status": 200, "remote": False, "ignored": "fixture"}
            with path.open("a") as stream:
                stream.write(json.dumps(attempt) + "\n" + json.dumps(success))
            self.assertEqual(audit.poll()["nativeLoginCompleted"], 0)
            with path.open("a") as stream:
                stream.write("\n")
            self.assertEqual(audit.poll(), {"nativeLoginCountsAvailable": True,
                                            "nativeLoginAudit": "official-log-markers",
                                            "nativeLoginAttempts": 1, "nativeLoginCompleted": 1,
                                            "nativeLoginRefused": 0})
            self.assertEqual(audit.poll()["nativeLoginCompleted"], 1)
            path.write_text("")
            with self.assertRaises(auth.Failure):
                audit.poll()

    def test_actual_distributed_bundle_has_no_native_login_audit_markers(self):
        entry = Path(__file__).resolve().parents[4] / "ekko-stock-compat-20261017/package/dist/server/index.js"
        if not entry.is_file():
            self.skipTest("Official npm package is not present in this checkout")
        self.assertEqual(hashlib.sha256(entry.read_bytes()).hexdigest(), auth.OFFICIAL_SHA256)
        actual = auth.bundle_capabilities(entry)
        self.assertFalse(actual["nativeLoginLogMarkers"])
        self.assertTrue(actual["ordinaryChatJwtContractVerified"])
        self.assertTrue(actual["globalAgentHasSeparateProcessLocalAgentToken"])

    def test_unknown_bundle_cannot_inherit_pinned_jwt_observation_contract(self):
        with tempfile.TemporaryDirectory() as folder:
            entry = Path(folder) / "index.js"
            entry.write_text("synthetic build: [mcu-login] completed [mcu-login] phase completed")
            actual = auth.bundle_capabilities(entry)
            self.assertTrue(actual["nativeLoginLogMarkers"])
            self.assertFalse(actual["ordinaryChatJwtContractVerified"])

    def test_unavailable_audit_never_opens_log_or_fabricates_zero_counts(self):
        audit = auth.NativeLoginAudit(Path("/absent/offline/server.log"), available=False)
        actual = audit.poll()
        self.assertFalse(actual["nativeLoginCountsAvailable"])
        self.assertEqual(actual["nativeLoginAudit"], "not_available")
        for key in ("nativeLoginAttempts", "nativeLoginCompleted", "nativeLoginRefused"):
            self.assertIsNone(actual[key])

    def test_observed_reauth_requires_verified_transition_and_fresh_server_tcp(self):
        for transition, tcp in ((True, True), (False, True), (True, False)):
            with self.subTest(transition=transition, tcp=tcp):
                run = auth.Recovery.__new__(auth.Recovery)
                run.capabilities = {"ordinaryChatJwtContractVerified": True}
                run.verified_auth_transition = transition
                run.device_health = Mock(return_value=({}, True))
                run.presence = Mock(return_value=True)
                run.new_server_has_m5_tcp = Mock(return_value=tcp)
                audit = auth.NativeLoginAudit(Path("/absent/offline/server.log"), available=False)
                with patch.object(auth.time, "monotonic", side_effect=[0, 1, 181]), \
                        patch.object(auth.time, "sleep"):
                    if transition and tcp:
                        actual = run.wait_m5(audit)
                        self.assertTrue(actual["automaticReauthenticationObserved"])
                        self.assertEqual(actual["recoveryEvidenceMode"], "derived-new-process-chat-jwt-handshake")
                        self.assertIsNone(actual["nativeLoginCompleted"])
                    else:
                        with self.assertRaises(auth.Failure):
                            run.wait_m5(audit)

    def test_tcp_observation_uses_only_current_pid_and_target_peer(self):
        run = auth.Recovery.__new__(auth.Recovery)
        run.pid, run.device_url = 12345, "http://192.168.50.3/health"
        output = (b"COMMAND PID USER FD TYPE DEVICE SIZE/OFF NODE NAME\n"
                  b"node 12345 user 40u IPv4 0x1 0t0 TCP 192.168.50.1:19038->192.168.50.3:50000 (ESTABLISHED)\n")
        with patch.object(auth, "command", return_value=Mock(stdout=output)) as command:
            self.assertTrue(run.new_server_has_m5_tcp())
            self.assertIn("12345", command.call_args.args[0])
        for old in (b":19038->192.168.50.3:", b"(ESTABLISHED)"):
            with patch.object(auth, "command", return_value=Mock(stdout=output.replace(old, b"mismatch"))):
                self.assertFalse(run.new_server_has_m5_tcp())

    def test_external_zombie_is_exited_without_waiting_on_signal_zero(self):
        run = auth.Recovery.__new__(auth.Recovery)
        run.pid, run.owned = 12345, {}
        with patch.object(auth, "process_stamp", return_value=("Z", "fixture")), \
                patch.object(auth, "listeners", return_value=[]), patch.object(auth.os, "kill") as kill:
            run.stop()
            kill.assert_not_called()

    def test_owned_child_is_explicitly_reaped(self):
        child = Mock()
        child.poll.return_value = 0
        run = auth.Recovery.__new__(auth.Recovery)
        run.pid, run.owned = 12345, {12345: child}
        run.stop()
        child.wait.assert_called_once_with()

    def test_verified_prelisten_process_can_be_stopped_for_finally_restoration(self):
        run = auth.Recovery.__new__(auth.Recovery)
        run.pid, run.owned = 12345, {}
        run.verify_process = Mock()
        with patch.object(auth, "process_stamp", side_effect=[("S", "fixture"), None]), \
                patch.object(auth, "listeners", return_value=[]), patch.object(auth.os, "kill") as kill:
            run.stop()
            run.verify_process.assert_called_once_with(require_listener=False)
            kill.assert_called_once_with(12345, signal.SIGTERM)

    def test_preflight_never_sends_a_login_post(self):
        run = auth.Recovery.__new__(auth.Recovery)
        run.host_jwt, run.allow_management_login = "offline-fixture", False
        run.http = Mock(return_value=(401, b""))
        run.management_login = Mock()
        with self.assertRaises(auth.Failure):
            run.presence()
        run.management_login.assert_not_called()

    def test_failure_after_signature_restart_always_calls_restoration(self):
        with tempfile.TemporaryDirectory() as folder:
            run = auth.Recovery.__new__(auth.Recovery)
            run.preflight = Mock()
            run.journal = Path(folder) / "absent.json"
            run.token_path = Path(folder) / ".token"
            run.token_path.write_bytes(b"offline-fixture")
            run.original, run.original_environment = {"userJwt": "offline-fixture"}, {}
            run.pid, run.birth, run.report = 12345, "fixture", {"stages": []}
            run.agent, run.last_uptime = "offline-fixture", 100
            run.audit_path = Path(folder) / "unused.log"
            run.save_state, run.stop, run.start, run.restore = Mock(), Mock(), Mock(), Mock()
            run.native_audit = Mock()
            run.http = Mock(return_value=(200, b""))  # Must have become401 after invalidation.
            with self.assertRaises(auth.Failure):
                run.execute("signature-rotation")
            run.restore.assert_called_once_with()

    def test_verified_process_environment_must_match_the_exact_launched_environment(self):
        run = auth.Recovery.__new__(auth.Recovery)
        run.pid, run.birth = 12345, "fixture-birth"
        run.home = Path("/private/isolated")
        run.original = {"node": "/private/node", "entry": "/private/entry.js"}
        run.expected_environment = {"AUTH_JWT_SECRET": "synthetic-expected"}
        with patch.object(auth, "listeners", side_effect=lambda port: [12345] if port == 19038 else []), \
                patch.object(auth, "process_stamp", return_value=("S", "fixture-birth")), \
                patch.object(auth, "process_arguments", return_value=(["/private/node", "/private/entry.js"],
                    {"AUTH_JWT_SECRET": "synthetic-different"})), \
                patch.object(auth, "validate_manifest"), patch.object(auth, "validate_environment"), \
                self.assertRaises(auth.Failure):
            run.verify_process()

    def test_audit_loss_never_prevents_normal_environment_and_auth_restoration(self):
        for lost_audit in (True, False):
            with self.subTest(lost_audit=lost_audit), tempfile.TemporaryDirectory() as folder:
                run = auth.Recovery.__new__(auth.Recovery)
                run.pid, run.original_environment = 12345, {}
                run.original, run.host_jwt = {"userJwt": "offline-fixture"}, "new-offline-fixture"
                run.path, run.audit_path = Path(folder) / "runtime.json", Path(folder) / "server.log"
                run.token_path = Path(folder) / ".token"
                run.token_path.write_bytes(b"offline-fixture")
                run.state = {"temporaryBindingObserved": True,
                             "tokenHash": hashlib.sha256(run.token_path.read_bytes()).hexdigest()}
                run.report = {}
                run.save_state = lambda **updates: run.state.update(updates)
                run.stop, run.start = Mock(), Mock()
                run.verify_process = Mock(return_value=(None, {}))
                run.http = Mock(return_value=(200, b""))
                run.management_login = Mock(return_value=30 * 24 * 3600)
                run.wait_m5 = Mock(side_effect=auth.Failure("Offline simulated log rotation"))
                run.native_audit = Mock(side_effect=FileNotFoundError()) if lost_audit else Mock()
                with patch.object(auth, "private_json"), \
                        self.assertRaises((auth.Failure, FileNotFoundError)):
                    run.restore()
                run.stop.assert_called_once_with()
                run.start.assert_called_once_with({})
                self.assertTrue(run.state["normalRestored"])
                self.assertTrue(run.report["restoration"]["authVerified"])
                self.assertFalse(run.report["restoration"]["deviceRecoveryVerified"])

    def test_unknown_bridge_is_never_stopped_or_reused(self):
        run = auth.Recovery.__new__(auth.Recovery)
        run.original, run.home = {"entry": "/private/package/dist/server/index.js"}, Path("/private/isolated")
        with patch.object(auth, "listeners", return_value=[12345]), \
                patch.object(auth, "process_arguments", return_value=(["python", "unknown.py"], {})), \
                patch.object(auth.os, "kill") as kill, self.assertRaises(auth.Failure):
            run.verify_bridge()
        kill.assert_not_called()

    def test_bridge_identity_accepts_only_the_official_isolated_runpy_bootstrap(self):
        script = Path("/private/clean/agent-bridge/python/hermes_bridge.py")
        home = Path("/private/isolated")
        root = "/private/existing-agent"
        arguments = [str(script), "--endpoint", "tcp://127.0.0.1:19039", "--agent-root", root,
                     "--hermes-home", str(home / "hermes")]
        env = {"HERMES_HOME": str(home / "hermes"), "HERMES_AGENT_ROOT": root,
               "HERMES_AGENT_BRIDGE_ENDPOINT": "tcp://127.0.0.1:19039"}
        code = (f"import sys, runpy; sys.path.insert(0, {root!r}); sys.argv = {arguments!r}; "
                f"runpy.run_path({str(script)!r}, run_name='__main__')")
        self.assertTrue(auth.official_bridge_process(["/private/python", *arguments], env, script, home))
        self.assertTrue(auth.official_bridge_process(["/private/python", "-I", "-c", code], env, script, home))
        for changed in (code + "; import os", code.replace("run_name='__main__'", "run_name='other'"),
                        code.replace("sys.path.insert(0", "sys.path.insert(1"),
                        code.replace("tcp://127.0.0.1:19039", "tcp://127.0.0.1:8748"),
                        code.replace(str(home / "hermes"), "/private/production")):
            with self.subTest(changed=changed[-30:]):
                self.assertFalse(auth.official_bridge_process(["/private/python", "-I", "-c", changed], env, script, home))
        self.assertFalse(auth.official_bridge_process(["/private/python", "-I", "-c", code],
            {**env, "HERMES_HOME": "/private/production"}, script, home))

    def test_transport_redirects_are_not_followed_and_budget_covers_official_startup(self):
        self.assertIsNone(auth.NoRedirect().redirect_request(None, None, 302, "", {}, "https://example.test"))
        self.assertEqual(auth.STARTUP_SECONDS, 180)
        self.assertFalse(auth.running_stamp(None))
        self.assertFalse(auth.running_stamp(("Z+", "fixture")))
        self.assertTrue(auth.running_stamp(("Ss", "fixture")))

    def test_http_reads_bypass_environment_proxy_and_have_fresh_nonsecret_urls(self):
        run = auth.Recovery.__new__(auth.Recovery)
        run.original = {"baseUrl": "http://127.0.0.1:19038", "profile": "default"}
        run.device_url = "http://192.168.50.3/health"
        response = Mock()
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=False)
        response.status, response.read.return_value = 200, b"{}"
        opener = Mock()
        opener.open.return_value = response
        with patch.object(auth.urllib.request, "build_opener", return_value=opener) as build:
            run.http("/api/auth/me", "synthetic-private-token")
            run.http("", device=True)
            run.http("", device=True)
        handlers = build.call_args.args
        self.assertTrue(any(isinstance(item, auth.urllib.request.ProxyHandler) and item.proxies == {}
                            for item in handlers))
        requests = [call.args[0] for call in opener.open.call_args_list]
        self.assertNotEqual(requests[1].full_url, requests[2].full_url)
        for request in requests:
            self.assertIn("_cardputer_auth_probe=", request.full_url)
            self.assertNotIn("synthetic-private-token", request.full_url)
            self.assertEqual(request.get_header("Cache-control"), "no-cache, no-store")

    def test_harness_cannot_issue_mcu_login_or_other_mutating_requests(self):
        run = auth.Recovery.__new__(auth.Recovery)
        run.original = {"baseUrl": "http://127.0.0.1:19038"}
        with patch.object(auth.urllib.request, "build_opener") as build:
            for path in ("/api/auth/mcu-login", "/api/auth/change-password", "/api/mcu-devices"):
                with self.subTest(path=path), self.assertRaises(auth.Failure):
                    run.http(path, body={"fixture": True})
            with self.assertRaises(auth.Failure):
                run.http("/clear", body={}, device=True)
        build.assert_not_called()

    def exercise_expiry_fixture(self, fault=None):
        with tempfile.TemporaryDirectory() as folder:
            run = auth.Recovery.__new__(auth.Recovery)
            run.preflight = Mock()
            run.journal = Path(folder) / "absent.json"
            run.token_path = Path(folder) / ".token"
            run.token_path.write_bytes(b"offline-persisted-signing-token")
            run.original, run.original_environment = {"userJwt": "normal-token", "username": "synthetic-user",
                                                       "password": "synthetic-password"}, {}
            run.pid, run.birth, run.report = 12345, "fixture", {"stages": [], "timeExpiryStageExecuted": False}
            run.agent, run.last_uptime = "fixture-agent", 100
            run.machine_identity = ("fixture-machine", "desktop", 19038)
            run.save_state = lambda **updates: run.state.update(updates)
            run.native_audit = lambda: auth.NativeLoginAudit(Path("/absent"), available=False)
            run.capabilities = {"ordinaryChatJwtContractVerified": True}
            run.device_health = Mock(return_value=({}, True))
            run.presence, run.new_server_has_m5_tcp = Mock(return_value=True), Mock(return_value=True)
            clock = {"now": 1000.0, "online": True, "environment": {}}
            trace = []
            def stop():
                trace.append(("stop", clock["now"]))
                clock["online"] = False
            def start(env):
                trace.append(("start", clock["now"], dict(env)))
                clock["environment"], clock["online"] = dict(env), True
            def http(path, token=None, body=None, **kwargs):
                self.assertTrue(clock["online"], "No expiry probes may run while the process is stopped")
                if path == "/api/auth/login":
                    ttl = int(clock["environment"].get("HERMES_WEB_UI_AUTH_JWT_EXPIRES_IN", "2592000s")[:-1])
                    payload = base64.urlsafe_b64encode(json.dumps({"iat": int(clock["now"]),
                        "exp": int(clock["now"]) + ttl}).encode()).decode().rstrip("=")
                    return 200, json.dumps({"token": "header." + payload + ".signature"}).encode()
                if token == "normal-token":
                    return 401, b""
                times = json.loads(base64.urlsafe_b64decode(token.split(".")[1] + "=="))
                return (401 if clock["now"] >= times["exp"] and fault != "expired_accepted" else 200), b"{}"
            def sleep(seconds):
                if fault == "interrupted":
                    raise KeyboardInterrupt()
                clock["now"] += seconds
            run.stop, run.start, run.http, run.restore = stop, start, http, Mock()
            with patch.object(auth.time, "monotonic", side_effect=lambda: clock["now"]), \
                    patch.object(auth.time, "time", side_effect=lambda: clock["now"] - (100 if fault == "clock_backwards" else 0)), \
                    patch.object(auth.time, "sleep", side_effect=sleep):
                if fault:
                    with self.assertRaises(KeyboardInterrupt if fault == "interrupted" else auth.Failure):
                        run.execute("time-expiry")
                    self.assertNotIn("passed", run.report)
                    self.assertFalse(any(stage["name"] == "time expiry" for stage in run.report["stages"]))
                else:
                    result = run.execute("time-expiry")
                    self.assertTrue(result["passed"])
                    self.assertTrue(result["timeExpiryStageExecuted"])
                    self.assertEqual(result["stages"][-1]["name"], "time expiry")
                    self.assertEqual(result["stages"][-1]["oldJwtHttp"], 401)
                    starts = [item for item in trace if item[0] == "start"]
                    self.assertEqual(len(starts), 2)
                    self.assertEqual(starts[0][2], starts[1][2])
                    self.assertGreaterEqual(starts[1][1] - starts[0][1], 47)
            run.restore.assert_called_once_with()
            self.assertNotIn("synthetic-password", json.dumps(run.report))
            self.assertNotIn("AUTH_JWT_SECRET", json.dumps(run.report))

    def test_true_expiry_keeps_server_off_until_tokens_expire_then_restores_normal(self):
        self.exercise_expiry_fixture()

    def test_expired_token_still_accepted_fails_and_restores(self):
        self.exercise_expiry_fixture("expired_accepted")

    def test_backwards_wall_clock_never_claims_real_expiry_and_restores(self):
        self.exercise_expiry_fixture("clock_backwards")

    def test_interrupt_while_server_is_off_always_restores(self):
        self.exercise_expiry_fixture("interrupted")


if __name__ == "__main__":
    unittest.main()
