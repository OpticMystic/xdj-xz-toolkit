import pathlib
import unittest

PACKAGE_ROOT = pathlib.Path(__file__).resolve().parents[1]
VENDOR = PACKAGE_ROOT / "vendor"
MODS = PACKAGE_ROOT / "mods"


class TakeoverModTests(unittest.TestCase):
    def test_rbp_symbol_prologues_match_guards(self):
        runtime_c = (MODS / "runtime.c").read_text(encoding="utf-8")
        self.assertIn("link_guard[8] = {0x70,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1}", runtime_c)
        self.assertIn("rekordbox_guard[8] = {0x38,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1}", runtime_c)
        self.assertIn("address == 0xdf994 ? link_guard", runtime_c)
        self.assertIn("address == 0xe0a50 ? rekordbox_guard", runtime_c)

        rbp_path = VENDOR / "decrypted_iso/pdj/extracted/pdj/rbp"
        if not rbp_path.is_file():
            self.skipTest("rbp binary is user-supplied in public repo")

        with rbp_path.open("rb") as f:
            # UiKey_Link at 0xdf994 (vaddr) -> file offset 0xdf994 - 0x8000
            f.seek(0x000df994 - 0x8000)
            link_prologue = f.read(8)
            expected_link = bytes([0x70, 0x40, 0x2d, 0xe9, 0x00, 0x50, 0xa0, 0xe1])
            self.assertEqual(link_prologue, expected_link, "UiKey_Link prologue mismatch in rbp")

            # UiKey_rekordbox at 0xe0a50 (vaddr) -> file offset 0xe0a50 - 0x8000
            f.seek(0x000e0a50 - 0x8000)
            rekordbox_prologue = f.read(8)
            expected_rekordbox = bytes([0x38, 0x40, 0x2d, 0xe9, 0x00, 0x50, 0xa0, 0xe1])
            self.assertEqual(rekordbox_prologue, expected_rekordbox, "UiKey_rekordbox prologue mismatch in rbp")

    def test_settings_c_schema_and_persistence(self):
        settings_h = (MODS / "settings.h").read_text(encoding="utf-8")
        self.assertIn("fb_takeover", settings_h)
        self.assertIn("takeover_assign", settings_h)
        self.assertIn("XZ_TAKEOVER_LINK = 0", settings_h)
        self.assertIn("XZ_TAKEOVER_REKORDBOX = 1", settings_h)
        self.assertIn("XZ_TAKEOVER_ONSCREEN = 2", settings_h)

        settings_c = (MODS / "settings.c").read_text(encoding="utf-8")
        self.assertIn("fb_takeover=%d", settings_c)
        self.assertIn("takeover_assign=%d", settings_c)
        self.assertIn("s->fb_takeover <= 1", settings_c)
        self.assertIn("s->takeover_assign <= 2", settings_c)
        self.assertIn("s.fb_takeover = 0", settings_c)
        self.assertIn("s.takeover_assign = 0", settings_c)

    def test_ui_h_actions_and_model(self):
        ui_h = (MODS / "ui/ui.h").read_text(encoding="utf-8")
        self.assertIn("XZ_UI_TAKEOVER_TOGGLE", ui_h)
        self.assertIn("XZ_UI_TAKEOVER_ASSIGN", ui_h)
        self.assertIn("int fb_takeover, takeover_assign, stems_overlay;", ui_h)
        self.assertIn("void xz_ui_render_vj_button(uint16_t *pixels,size_t stride,int takeover_active);", ui_h)

    def test_mixer_receive_hook_is_in_runtime_allowlist(self):
        runtime = (MODS / "runtime.c").read_text(encoding="utf-8")
        self.assertIn("address == 0x25d7f4 ? mixer_receive_guard", runtime)
        self.assertIn("mixer_receive_guard[8] = {0x40,0x32,0xd0,0xe5,0xf0,0x47,0x2d,0xe9}", runtime)

    def test_native_touch_vj_button_interception(self):
        touch_h = (MODS / "ui/native_touch.h").read_text(encoding="utf-8")
        self.assertIn("int vj_btn_x,vj_btn_y,vj_btn_w,vj_btn_h;", touch_h)

        touch_c = (MODS / "ui/native_touch.c").read_text(encoding="utf-8")
        self.assertIn("t->vj_btn_x=0;t->vj_btn_y=0;t->vj_btn_w=112;t->vj_btn_h=24;", touch_c)
        self.assertIn("t->model&&vj_btn(t,s)", touch_c)
        self.assertIn("act={.kind=XZ_UI_TAKEOVER_TOGGLE}", touch_c)

    def test_ui_runtime_bridge_and_hooks(self):
        ui_runtime_h = (MODS / "ui_runtime.h").read_text(encoding="utf-8")
        self.assertIn("void xz_ui_runtime_on_source_key(int source);", ui_runtime_h)

        ui_runtime_c = (MODS / "ui_runtime.c").read_text(encoding="utf-8")
        self.assertIn("int xz_mods_takeover_v1(void)", ui_runtime_c)
        self.assertIn("void xz_ui_runtime_on_source_key(int source)", ui_runtime_c)
        self.assertIn("xz_hook_arm(0xdf994, link_guard", ui_runtime_c)
        self.assertIn("xz_hook_arm(0xe0a50, rekordbox_guard", ui_runtime_c)
        self.assertIn("XZ_UI_TAKEOVER_TOGGLE", ui_runtime_c)
        self.assertIn("XZ_UI_TAKEOVER_ASSIGN", ui_runtime_c)
        self.assertIn("xz_ui_render_native_buttons(pixels,stride,model.theme,model.stems_overlay,", ui_runtime_c)
        self.assertIn("model.connection.enabled&&model.connection.connected,model.fb_takeover", ui_runtime_c)

    def test_directfb_hook_takeover_suppression(self):
        bridge_h = (VENDOR / "tools/xz_runtime/mods_bridge.h").read_text(encoding="utf-8")
        self.assertIn("typedef int (*xz_mods_takeover_fn_v1)(void);", bridge_h)
        self.assertIn("int xz_mods_takeover_v1(void);", bridge_h)

        directfb_c = (VENDOR / "tools/xz_runtime/xz_directfb_hook.c").read_text(encoding="utf-8")
        self.assertIn("static xz_mods_takeover_fn_v1 mods_takeover;", directfb_c)
        self.assertIn('mods_takeover = (xz_mods_takeover_fn_v1)dlsym(RTLD_DEFAULT, "xz_mods_takeover_v1");', directfb_c)
        self.assertIn("if (mods_takeover && !mods_takeover()) return 0;", directfb_c)


if __name__ == "__main__":
    unittest.main()
