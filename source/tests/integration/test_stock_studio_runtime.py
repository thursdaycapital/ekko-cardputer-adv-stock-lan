"""Offline seed/scope safety checks; no server, device or provider calls."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import yaml
import stock_studio_runtime as runtime


class IsolatedRuntimeTests(unittest.TestCase):
    def test_app_scope_rejects_an_unrelated_package_or_node_before_execution(self):
        app = Path("/private/fixture/Ekko Studio.app")
        package = app / "Contents/Resources/webui"
        helper = app / "Contents/Frameworks/Ekko Studio Helper.app/Contents/MacOS/Ekko Studio Helper"
        with patch.object(runtime.subprocess, "run") as run:
            for actual_package, actual_node in ((Path("/private/other"), helper), (package, Path("/usr/bin/node"))):
                with self.subTest(package=actual_package), self.assertRaises(RuntimeError):
                    runtime.verify_signed_app(app, actual_package, actual_node)
        run.assert_not_called()

    def test_seed_imports_model_voice_but_never_messengers_tools_or_relay_settings(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            seed, private = root / "seed", root / "runtime"
            for relative in (".hermes", ".hermes-web-ui/.ekko/config"):
                (seed / relative).mkdir(parents=True)
            for relative in ("hermes", "webui/.ekko/config"):
                (private / relative).mkdir(parents=True)
            (seed / ".hermes/config.yaml").write_text(yaml.safe_dump({"model": {"name": "fixture"},
                "tts": {"provider": "fixture"}, "telegram": {"token": "not-imported"},
                "mcp_servers": {"private": {}}, "plugins": ["not-imported"]}))
            (seed / ".hermes-web-ui/config.json").write_text(json.dumps({"customModels": ["fixture"],
                "appRelayRoute": "not-imported", "gatewayAutoStart": {"enabled": True}}))
            (seed / ".hermes-web-ui/.ekko/config/config.json").write_text(json.dumps({"schemaVersion": 1,
                "model": {"defaultModel": "fixture"}, "tools": {"enabled": ["not-imported"]}}))
            runtime.seed_model_voice(seed, private)
            actual = yaml.safe_load((private / "hermes/config.yaml").read_text())
            self.assertEqual(actual, {"model": {"name": "fixture"}, "tts": {"provider": "fixture"}})
            actual = json.loads((private / "webui/config.json").read_text())
            self.assertEqual(actual, {"customModels": ["fixture"], "gatewayAutoStart": {"enabled": False}})
            actual = json.loads((private / "webui/.ekko/config/config.json").read_text())
            self.assertEqual(actual, {"schemaVersion": 1, "model": {"defaultModel": "fixture"}})
            for path in private.rglob("*"):
                if path.is_file():
                    self.assertEqual(path.stat().st_mode & 0o777, 0o600)


if __name__ == "__main__":
    unittest.main()
