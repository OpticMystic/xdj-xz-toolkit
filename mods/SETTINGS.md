# USB preferences

The loader passes its mounted USB root in `XZ_MODS_USB`. Preferences are stored
at `VJ.Tools/XZ-Mods.cfg` on that USB. The audio/input callbacks never write files.
The settings worker atomically replaces a complete version1 record and reports
save success or failure in MODS. An absent file uses defaults; an invalid record
is rejected as a whole. Settings are shared by both internal decks.

```
XZ_MODS_SETTINGS 1
stems=1
gate=0
smart=0
theme=0
stem_page=0
shift_pages=0
pad_feedback=1
shift_keysync=0
fb_takeover=1
takeover_assign=0
```

`fb_takeover`: 1 (enabled, default), 0 (disabled). Controls whether the VJ.Tools video and composite frame takes over the hardware display framebuffer. When disabled, the native Pioneer XDJ-XZ playback/browse screen is displayed uninterrupted.

`takeover_assign`: 0 (LINK physical button, default), 1 (REKORDBOX physical button), 2 (Onscreen VJ.Tools button in opposite corner from MODS). Selects which control toggles FB display takeover on and off.


`stem_page`: 0 Hot Cue, 1 Beat Loop, 2 Slip Loop, 3 Beat Jump. A/B/C toggle
drums/harmonics/vocals; D toggles bypass while the STEMS controls own the deck.
With `shift_pages=1`, Shift plus the four page buttons provides those four
actions in the same order. Releases remain owned even if Shift is released
first or the panel closes. Unmodified page buttons retain their native action.

`pad_feedback=1` selects the UI theme's stem RGB colours, dimming muted pads.
`shift_keysync=1` reserves Shift+Sync for Key Sync; automatic pitch matching is
currently unavailable pending verified track-key/master metadata. New USBs
default that setting to0. Per-track mute/bypass and temporary key shifts are
performance state and are not restored on boot.

Hardware acceptance still requires a saved preference readback and player
restart with the same USB. Host and isolated ARM filesystem tests do not prove
USB removal or physical power-loss behaviour.
