import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

PACKAGE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE / "vendor"))
from tools.xz_firmware.payload import write_runtime_payload


class ModPayloadTests(unittest.TestCase):
    def test_default_payload_does_not_add_standalone_runtime(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            result = write_runtime_payload(root, b"fixture-app", features=("receiver",))
            self.assertEqual(result["mods_md5"], "")
            self.assertFalse((root / "mods-mode").exists())
            self.assertFalse((root / "tools/libxz-mods.so").exists())

    def test_explicit_runtime_is_staged_with_hash_and_profile(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runtime, receiver = root / "runtime.so", root / "receiver.so"
            runtime.write_bytes(b"runtime-fixture")
            receiver.write_bytes(b"receiver-fixture")
            stage = root / "stage"
            result = write_runtime_payload(stage, b"app", features=("mods",),
                        directfb_hook_path=receiver, mods_runtime_path=runtime, mods_mode="observer")
            self.assertEqual((stage / "mods-mode").read_bytes(), b"observer\n")
            self.assertEqual((stage / "tools/libxz-mods.so").read_bytes(), runtime.read_bytes())
            self.assertEqual(result["mods_md5"], hashlib.md5(runtime.read_bytes()).hexdigest())
            self.assertNotIn(b"\r", (stage / "autoexec.sh").read_bytes())
            with self.assertRaises(ValueError):
                write_runtime_payload(root / "bad", b"app", features=(), mods_runtime_path=runtime)
            with self.assertRaises(ValueError):
                write_runtime_payload(root / "bad", b"app", features=(), mods_mode="unknown")

    def test_real_launcher_selects_requested_profile(self):
        shell = shutil.which("bash")
        if os.name == "nt":
            shell = "C:/Program Files/Git/bin/bash.exe"
        if not shell or not Path(shell).exists():
            self.skipTest("POSIX shell required for launch contract test")
        source = (PACKAGE / "vendor/tools/xz_runtime/orchestrator.sh").read_text()
        body = source.split("launch_runtime_rbp() {\n", 1)[1].split("\n}\n", 1)[0]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "receiver.so").write_bytes(b"test fixture")
            (root / "runtime.so").write_bytes(b"test fixture")
            body = body.replace("cd /root/pdj", "cd " + shlex.quote(root.as_posix()))
            body = body.replace("/tmp/xz_rbp.log", (root / "launch.log").as_posix())
            body = body.replace("./rbp ", "fixture_rbp ")
            # The fixture captures the actual launch environment; it does not
            # execute a DJ application or any firmware startup side effects.
            for mode, expected in (("", "unset|unset|unset|"), ("observer", "1|1|0|"), ("experimental", "1|0|1|")):
                result = root / "result"
                script = root / "test.sh"
                script.write_text(
                    'collect_rbp_options() { tsc_option=; joglcd_option=; nfs_options=; }\nlog() { :; }\n'
                    + 'fixture_rbp() { printf "%s|%s|%s|%s" "${XZ_MODS_ENABLE-unset}" '
                    + '"${XZ_MODS_OBSERVER-unset}" "${XZ_MODS_UI-unset}" "$LD_PRELOAD" > "$RESULT"; }\n'
                    + "HOOK_RAM=" + shlex.quote((root / "receiver.so").as_posix()) + "\n"
                    + "MODS_RAM=" + shlex.quote((root / "runtime.so").as_posix()) + "\n"
                    + "MODS_MODE=" + shlex.quote(mode) + "\n"
                    + "export RESULT=" + shlex.quote(result.as_posix()) + "\n"
                    + "launch_runtime_rbp() {\n" + body + "\n}\nlaunch_runtime_rbp\nwait \"$RBP_LAUNCH_PID\"\n", newline="\n")
                env = {k: v for k, v in os.environ.items() if not k.startswith("XZ_MODS_")}
                completed = subprocess.run([shell, str(script)], env=env, capture_output=True, text=True)
                self.assertEqual(completed.returncode, 0, completed.stderr + (root / "launch.log").read_text())
                self.assertTrue(result.read_text().startswith(expected), (mode, result.read_text()))


if __name__ == "__main__":
    unittest.main()
