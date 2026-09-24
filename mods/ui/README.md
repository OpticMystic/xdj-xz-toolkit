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
eight sample pads, all enable settings, server settings and seven theme choices.
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

With stems enabled, the selected bank controls Vocals, Harmonics, Drums, then
bypass on that physical deck. A-D is the default; E-H is an optional saved
setting in MODS > Controls. The inactive bank keeps its normal hot cues. Screen
labels, pad LEDs and touch actions follow the selected bank. The audio engine
retains its internal Drums/Harmonics/Vocals storage order. Both decks stay
independent while MODS or the inline strip is hidden. Turning stems off restores
normal pad dispatch, including paired releases for presses already owned by the
mod.

Touch and pad mute controls share one latch. In the inline strip, tapping a
stem toggles it and dragging adjusts its level. Both the inline strip and full
panel show Vocals, Harmonics, Drums. Inline waveform placement uses the same
widget layout as the labels and touch targets.

`test_pad_order.c` checks the physical A/B/C/D routing against onscreen actions
on both decks and checks the full-panel order. Pad-state checks cover both
banks, inactive-bank hot cues and release ownership across bank changes.
`test_runtime_controls.c` checks touch/pad interoperability, LED colors, scene
ownership and display bounds.
Physical acceptance of each new runtime remains separate from these tests.

## Spare channel stem EQ

In MODS > Controls, enable Spare Channel Stem EQ. Channel 3 controls Deck 1
and channel 4 controls Deck 2. HIGH controls Vocals, MID Harmonics, LOW Drums.
Centre and clockwise positions retain full volume; turning left fades to zero.
Each knob picks up the current stem level before it takes control, so enabling,
resuming, loading a track or using touch controls cannot cause a gain jump.

The mode reads the XZ's ordinary mixer MIDI reports inside the player and passes
them through unchanged. It does not need a DJ application mapping or desktop
relay. The XZ Utility's Mixer MIDI Message must be set to Send or Send with Time
Param. A mapping requires a local USB track on its deck and PC selected on its
spare mixer channel. LINK/PC-deck sources, unknown sources and external channel
inputs suspend control. The UI remains Waiting until a valid mixer report
arrives. Suspension leaves the current stem levels unchanged and resets knob
pickup.

The firmware 1.26 adapter hooks the normal MIDI CC dispatch path. It does not
request the factory volume-test mode, which changes audio routing. Source and
input guards, telemetry mapping, stock MIDI passthrough and refusal when the
guarded hook is unavailable are covered by isolated checks. Physical audio
acceptance and cold boot remain separate checks for each released runtime.

## Enter and leave the VJ.Tools view

The top-left VJ.Tools button appears only while the MODS VJ CONNECTION setting
is enabled and the network connection is live.
It reads VJ.Tools on the native screen and Exit VJ while the stream view is
enabled. The full VJ.Tools settings page stays in MODS even when the network is
offline. The physical shortcut assignment is optional. Exiting keeps incoming
frames from taking over until the user opens the view again. View choice is
saved when the settings USB is writable.

Native-view touches stay native; streamed-view touches go to the desktop.
A gesture keeps its owner until release even when it crosses the view button
or a source shortcut changes the view.
