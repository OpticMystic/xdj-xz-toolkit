# Native 800x480 mod controls

See [NATIVE_STYLE.md](NATIVE_STYLE.md) for the native-style typography, colours,
font provenance and local preview workflow. Track each deployed runtime and
physical acceptance separately from portable UI tests.

`ui.c` renders RGB565 into a caller-owned framebuffer. It has no DirectFB,
device, audio, network or allocation dependency. The runtime supplies a snapshot
and receives typed actions; the renderer never assumes a feature is implemented.

## Runtime contract

1. Zero-initialize `xz_ui_model`; initialize each deck's `groove_active` and
   `sample_active` to -1, levels and sample volume to 1. Call `xz_ui_init` once.
2. Set each deck's `ready` bits only for connected, validated handlers. Keep
   `enabled` separate: implemented and enabled are different states. Deck 3/4
   labels describe external PC sources, not additional onboard players.
   The VJ connection snapshot is device-wide and independent of selected deck.
3. Feed pointer down/move/up into `xz_ui_touch`. Execute returned actions, then
   publish confirmed values in the next model. Do not optimistically enable a
   setting that failed. A refused action returns `XZ_UI_UNAVAILABLE` and the
   footer explains that readiness is missing.
4. Render with `xz_ui_render`. Stride is **pixels**, buffer count is **uint16_t
   elements**. It returns zero for insufficient size or invalid selected deck.
   Present through the host-owned display adapter without touching the existing
   VJ receiver's surface.
5. Call `xz_ui_cancel` and execute its release action on focus loss, overlay
   removal, track/device removal or teardown. Serialize touch, model snapshots
   and render on the UI thread.

`XZ_UI_PANEL` carries the old page in `index` and new page in `value`. Closing
X-PAD must stop its sound and restore the physical pad/memory/delete/volume
roles. Closing MODS or hiding the STEMS strip preserves independent stem pad control
on both decks while stems are enabled. Disabling stems restores normal hot cues. `XZ_UI_DECK` carries old deck in `deck` and new
deck in `index`; runtime must release old deck panel ownership and establish
the new one. `XZ_UI_CLOSE` returns the screen to the host's stock interface.
The pure UI cannot execute any of these engine transitions itself.

| Action | Payload and semantics |
|---|---|
| ENABLE | `index` is capability bit, `value` is requested 0/1 |
| LEVEL / VOLUME | normalized 0..1 `value`, PRESS/MOVE |
| MUTE | stem index 0..2, PRESS toggles the shared touch/pad mute latch |
| BYPASS | requested 0/1; runtime chooses original audio |
| GROOVE_PAD | slot 0..7; runtime toggles it and releases any previous slot |
| SAMPLE_PAD / HOTCUE_PAD | slot 0..7 and PRESS/RELEASE |
| STRIP | loop index 0..5, loop beats in `value`, pitch -12..12 in `secondary`; RELEASE ends gesture unless runtime HOLD is on |
| HOLD / OVERDUB | requested 0/1; disabling overdub clears the event sequencer |
| SET_THEME | theme index 0..6 |
| SERVER_AUTO | requested AUTO=1 / MANUAL=0 |
| SERVER_ADDRESS | request the host's address editor; no keyboard is claimed here |
| KEY_SHIFT / KEY_SYNC | signed semitone step / sync request; capability gated |
| CONNECTION_ENABLE / DISCOVERY | requested 0/1; receiver callback must confirm before the model changes |

Momentary controls retain pointer capture when dragged outside their rectangle.
Release targets the original deck and control even if runtime readiness changes.
Unassigned Groove slots emit normal HOTCUE actions; assigned but unloaded slots
remain NOT READY. `groove_assigned` and `groove_loaded` are therefore distinct.
The runtime remains responsible for physical-pad dispatch and mutual ownership
between hot cues, Groove and X-PAD.

The five tabs expose three stem faders, hold-to-mute labels, bypass, modification
dot, eight Groove pads, six X-PAD lengths, pitch strip, HOLD, OVERDUB, volume,
eight sample pads, capability-aware settings and twelve theme choices.
The VJ.Tools Connection tab remains visible even when no receiver integration is
loaded. It shows receiver-reported readiness, connection state, device IP, port,
protocol and frame rate. Unknown values read NOT REPORTED. `connection.ready`
means the runtime has a receiver status provider; `can_enable` and `can_discover`
mean the corresponding control callback exists. Read-only status works without
these callbacks. Frame rate is shown only when connected and `stats_valid`.

Zero-initialize the connection snapshot until actual receiver data is available.
`CONNECTION_ENABLE` and `DISCOVERY` only request changes; they neither start a
new receiver nor replace the existing protocol. The VJ.Tools connection is
optional: native DJ controls must retain their independent readiness and work
without a desktop. The header carries VJ.Tools branding, and the Connection
page credits cdj3k-mods, nsaintot and contributors.

Waveform peaks come from runtime data; absent data is explicitly labeled.
No sample names, waveforms or feature success are invented.

## Verification

```powershell
.tmp/cdj-zig/ziglang/zig.exe cc -O2 -UNDEBUG -Wall -Wextra -Werror packages/xdj-xz-toolkit/mods/ui/ui.c packages/xdj-xz-toolkit/mods/ui/test_ui.c -o .tmp/xz-port-build/ui-test.exe
.tmp/xz-port-build/ui-test.exe .tmp/xz-port-build
.tmp/cdj-zig/ziglang/zig.exe cc -target arm-linux-gnueabi.2.13 -mcpu=cortex_a9 -O2 -Wall -Wextra -Werror -c packages/xdj-xz-toolkit/mods/ui/ui.c -o .tmp/xz-port-build/ui-arm.o
```

`test_ui` checks actions, held releases, all six strip bins, pitch endpoints,
capability refusal, unassigned/failed Groove slots, receiver callback readiness,
deck-independent connection controls, all themes and buffer bounds.
`--prove-assertions` intentionally fails; the build also rejects NDEBUG.
Passing tests produce five PPM screen captures plus theme/readiness examples.
The connection demo has no reported receiver and never invents a live connection.
Host rendering and ARM object
compilation passed. DirectFB presentation, physical touch routing, audio actions
and complete stock-screen recoloring remain separate runtime acceptance gates.
## Physical stem pads

With stems enabled, the selected bank and pad mode control Vocals, Harmonics,
Drums, then bypass on that physical deck. Other pad modes remain native,
including Hot Cue when a different mode is selected. A-D is the default; E-H is an optional saved
setting in MODS > Controls. The inactive bank keeps its normal hot cues. Screen
labels, pad LEDs and touch actions follow the selected bank. The audio engine
retains its internal Drums/Harmonics/Vocals storage order. Both decks stay
independent while MODS or the inline strip is hidden. Turning stems off restores
normal pad dispatch, including paired releases for presses already owned by the
mod.

Touch and pad mute controls share one latch. In the inline strip, tapping a
stem toggles it and dragging adjusts its level. Raising a muted stem with a
drag unmutes it. Inline waveform placement uses the same
widget layout as the labels and touch targets.

`test_pad_order.c` checks the physical A/B/C/D routing against onscreen actions
on both decks and verifies the full panel no longer duplicates the mixer. Pad-state checks cover both
banks, inactive-bank hot cues and release ownership across bank changes.
`test_runtime_controls.c` checks touch/pad interoperability, LED colors, scene
ownership and display bounds.
Physical acceptance of each new runtime remains separate from these tests.

## Two-deck stem controls on the native screen

The 536x268 native waveform window keeps both deck waveforms. It places a
48-pixel control row immediately after each waveform. Every row has four
buttons in the same order as the physical stem pad bank: Vocals, Harmonics,
Drums, and Bypass. Eight touch targets stay visible together on the play
screen, with no deck-switch button.

Tap a stem to mute it. Drag horizontally inside a stem button to set its
level. A held touch keeps its starting deck and stem when it crosses a row
boundary. Touches that begin on either waveform stay with the native waveform
gesture. Bypass applies only to its own deck.

The MODS panel keeps the pad-bank and show settings. Closing it or hiding the
inline rows does not change active stem levels or physical pad assignments.
The dual-row layout has portable render and touch checks; hardware audio and
screen acceptance must be recorded separately.

## Streamlined menu and themes

MODS opens to Controls, with Appearance, VJ.Tools and Advanced as its other
main pages. The full-screen stem mixer is removed; Groove pads and X-PAD
remain available under Advanced with truthful readiness indicators.
Each inline stem shows its volume percentage and accepts a horizontal drag.

The original seven themes keep their saved IDs. Game Boy, Super Nintendo,
Windows 95, Game Boy Color and Aqua / iTunes add original interface chrome;
the two handheld themes also use an original pixel font. Themes affect MODS
and the inline controls, not the complete stock firmware interface.

Development deployments can use `--ram-settings` to read existing settings
while keeping changes in RAM. This avoids writing a newer theme ID into the
older USB runtime's preferences; reboot restores the original settings.

## Enter and leave the VJ.Tools view

The top-left VJ.Tools button appears only while the MODS VJ.TOOLS CONNECTION setting
is enabled and the network connection is live.
It reads VJ.Tools on the native screen and Exit VJ while the stream view is
enabled. The full VJ.Tools settings page stays in MODS even when the network is
offline. The physical shortcut assignment is optional. Exiting keeps incoming
frames from taking over until the user opens the view again. View choice is
saved when the settings USB is writable.

Native-view touches stay native; streamed-view touches go to the desktop.
A gesture keeps its owner until release even when it crosses the view button
or a source shortcut changes the view.
