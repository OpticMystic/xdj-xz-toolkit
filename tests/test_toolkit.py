import pathlib
import unittest


PACKAGE_ROOT = pathlib.Path(__file__).resolve().parents[1]
VENDOR = PACKAGE_ROOT / "vendor"


class ToolkitClosureTests(unittest.TestCase):
    def test_public_tooling_is_present(self):
        for relative in (
            "tools/xz_cli.py",
            "tools/xz_runtime/xz_directfb_hook.c",
            "tools/xz_runtime/orchestrator.sh",
            "builder/firmware.py",
            "mods/build.py",
        ):
            self.assertTrue((VENDOR / relative).is_file() or (PACKAGE_ROOT / relative).is_file(), relative)

    def test_private_firmware_inputs_are_not_shipped(self):
        for relative in (
            "keys/aes256.key",
            "decrypted_iso/pdj/extracted/pdj/rbp",
            "build/imagedata-vjtools-loader.dat",
            "build/libxz-directfb-hook-v49-exclusive-abi14.so",
            "build/xz-gui-ip-patch",
        ):
            self.assertFalse((VENDOR / relative).is_file(), relative)

    def test_private_input_placeholders_exist(self):
        self.assertTrue((VENDOR / "keys/README.md").is_file())
        self.assertTrue((VENDOR / "decrypted_iso/README.md").is_file())

    def test_no_external_toolkit_path_is_required_by_cli(self):
        cli = (VENDOR / "tools/xz_cli.py").read_text(encoding="utf-8")
        self.assertIn("ROOT_DIR = pathlib.Path(__file__).resolve().parent.parent", cli)
        self.assertNotIn("E:\\\\Github\\\\XDJXZMe", cli)


if __name__ == "__main__":
    unittest.main()
