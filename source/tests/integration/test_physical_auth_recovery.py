"""Offline safety regressions. No listener, device, model or voice requests."""
import hashlib
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
                run.execute()
            run.restore.assert_called_once_with()

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

    def test_transport_redirects_are_not_followed_and_budget_covers_official_startup(self):
        self.assertIsNone(auth.NoRedirect().redirect_request(None, None, 302, "", {}, "https://example.test"))
        self.assertEqual(auth.STARTUP_SECONDS, 180)
        self.assertFalse(auth.running_stamp(None))
        self.assertFalse(auth.running_stamp(("Z+", "fixture")))
        self.assertTrue(auth.running_stamp(("Ss", "fixture")))


if __name__ == "__main__":
    unittest.main()
