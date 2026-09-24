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
# Physical stem pads

With stems enabled, open STEMS / GC and select deck 1 or 2. In that deck's
HOT CUE mode, A toggles drums, B harmonics, C vocals, and D bypass. Mutes latch
until pressed again and remain in effect when the panel closes. A held
touchscreen mute is independent of the pad latch. E-H retain normal hot cues.
Closing the panel restores normal pad dispatch after any owned press releases.
Pads on the other deck retain normal behavior.

Run `python ui/verify.py --zig <zig>` from the mods directory for portable
control, touch and rendering checks. Physical pad acceptance is still required
for the new build. Pad LEDs are not yet connected to stem state.

The current panel is full-screen. Normal USB playback integration remains in
progress; see `NATIVE_LAYOUT.md` for the grounded native layout functions and
the missing waveform reflow contract. `capture_native_screen.py <new-dir>`
captures a framebuffer and raw native state without modifying the application.
