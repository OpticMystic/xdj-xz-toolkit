# XZ inline stems investigation, 2026-09-08

Read-only investigation. No device writes, restarts, or source edits performed.

## Result

The current full-screen MODS panel does not meet the requested native USB integration. The upstream CDJ mod borrows the application's quick-menu band and invokes its native layout mode. A safe XZ port should attach a stems strip to the XZ play-screen lifecycle and reclaim space through its native drawing system. A generic framebuffer overlay or assumed spare rectangle would cover native information and is not equivalent.

## Upstream evidence

https://cdj3k-mods.com/ was refreshed with web search during this task. The indexed primary page describes the play screen with X-PAD/STEMS in the quick menu, three faders, and GATE CUE. Local upstream source and screenshot provide exact behavior:

- `.tmp/cdj3k-mods/web/public/img/overview.png`, inspected visually: native track header, waveform, time/BPM/key, full-track waveform remain visible around an open three-fader row. STEMS is a native-looking title-bar quick-menu button.
- `.tmp/cdj3k-mods/package/deck/mods/stem/ui/build.c`, `stems_build`: attaches using `kit_band_attach(anchor)`, builds a button and row in the existing waveform view.
- `.tmp/cdj3k-mods/package/deck/mods/kit/band.c:31-43`: the native quick-menu mode changes the waveform's internal redraw scale; changing component bounds alone produced different drawing even with identical geometry.
- `band.c:544` onward: preferred path borrows the application's quick-menu mode; native menu takeover closes mod strip.
- `mods/stem/ui/hooks.c`: persistent edited-state indicator remains on STEMS while row is closed, and control state remains active.

The CDJ uses JUCE visual components. The XZ symbol inventory shows older C `ui_PLAYMODE_*` and `DS_GR_*` drawing/window routines. Copying CDJ addresses or component layouts is invalid.

## Exact XZ evidence

Inspected `.tmp/xz-port-live/rbp.running` using pyelftools plus Capstone ARM disassembly. These are ELF virtual addresses, not offsets. Symbol names are proven. Mutating call contracts still require call-site and runtime validation.

| Symbol | Address | Evidence |
|---|---:|---|
| `ui_PLAYMODE_Set` | `0x001fc2fc` | 6376-byte function; reads `st_ui_playmode_wave_data + 0x7c` and initializes root draw objects |
| `ui_PLAYMODE_Clear` | `0x001fdbe4` | 184 bytes; hides/refreshes object IDs `0x100`, `0x109`, `0x10d`; clears lifecycle flag +0x7c |
| `st_ui_playmode_wave_data` | `0x01b31ed0` | Exact object symbol; literal at `0x1fd01c` and `0x1fdc98` points here |
| `ui_com_draw_GetObjectByID` | `0x00142ec0` | 88 bytes; accepts optional object pointer in r0 and ID in r1; null parent walks current root |
| `ui_com_draw_RefreshObject` | `0x00143274` | 116 bytes; called on the three root IDs above during play clear |
| `DS_GR_CreateWindow` | `0x00157344` | 860-byte native draw-window constructor |
| `DS_GR_GetWindowInfo` | `0x00155528` | 116 bytes; r0 window, r1 output; writes width/height u16 at output+8/+10, additional fields at +12..+24; does not populate output+0..+7 |
| `DS_GR_MoveWindow` | `0x00155130` | 180 bytes; r0 window, r1 x, r2 y; updates window +0x30/+0x34 and endpoint +0x38/+0x3c; calls task layer |
| `DS_GR_SetWindowRegion` | `0x0015559c` | 188 bytes; sets bounded crop region, not arbitrary resampling; validates endpoints against window dimensions |
| `ui_Counter_CreateWaveWindow` | `0x0022db9c` | 372 bytes; builds a 300x34 native wave window via DS_GR_CreateWindow |
| `GetGuiState` | `0x0016f2c4` | 16 bytes; returns the pointer stored in `pCurState` at `0x01b26928`; this is a state object pointer, not a screen enum |

Do not confuse the summary 300x34 counter-wave window with the large scrolling waveform. No large-wave reflow contract is proven here. The +0x7c play lifecycle flag is not yet proven sufficient to distinguish every overlay/browser mode.

## Saved screen limitation

Visually inspected `live-fader-baseline.png` and `live-fader-closed-check.png`. Both still contain the full-screen MODS panel despite their names. They cannot establish stock screen geometry or a safe insertion rectangle. Main agent was notified and asked to capture fb0 after closing MODS during its own live check. No new screenshot arrived before the report was written.

## Minimal next changes

1. Main agent's pad mapping can ship independently through the existing exact `PlayerInnards::onPhysicalKey` adapter. Show A/B/C/D labels in the stems control row and track selected physical deck. Consume paired releases even when the panel closes, and preserve unrelated hot cues.
2. Add read-only native display-state observations at the proven play Set/Clear boundaries, with exact prologue guards. Identify a proved screen discriminator within the current GUI state or its owning controller, and log native draw-object bounds while showing normal play, browser, menus, and returning to play. This produces an actual XZ layout contract before changing geometry.
3. Implement inline stems rendering only after identifying the native play-screen region and native waveform redraw/layout path. Prefer a native draw-window strip integrated in that lifecycle. Keep the stock browser and waveform/time/counter objects intact, and restore their original layout on close.
4. The touch adapter currently captures every contact whenever `visible` is true. Inline mode must capture only new presses inside the strip or STEMS button. A stock drag that began outside must remain stock until release; an owned mod drag remains mod until release even if it leaves the strip or the strip closes. Use actual final geometry for hit testing. All coordinates outside the strip should reach stock unchanged unless a proved native reflow requires remapping.
5. Keep full-screen settings as a separate view. A small persistent STEMS edited marker can coexist with the native play screen, but must not be described as completed inline controls.

## Reject these shortcuts

- Drawing a row across the bottom without stock geometry evidence.
- Scaling the current framebuffer in place every flip: retained mod pixels can be captured repeatedly and stock partial updates make the background stale.
- Keeping the existing receiver presenter as the background source: its composite buffer is the VJ frame, not a pristine current native play screen.
- Reusing upstream JUCE ABI, or treating DS_GR_SetWindowRegion as waveform relayout.

No full inline implementation or physical acceptance claim follows from this investigation.

## Correction: GetGuiState returns a pointer

A follow-up exact symbol/call-site check proves `0x01b26928` is the 4-byte `STT_OBJECT` symbol `pCurState`. `GetGuiState` executes:

```text
0x16f2c4 movw r3, #0x6928
0x16f2c8 movt r3, #0x1b2
0x16f2cc ldr r0, [r3]
0x16f2d0 bx lr
```

It loads the pointer value in the cell once and returns it; it does not dereference a field within the pointed-to state. Concrete callers prove pointer semantics. `GetCurrentMedia_Link` calls it at `0x1674d8`, then reads `[r0, #0xd0]` at `0x1674dc` and `[r0, #0xc8]` at `0x1674e8`. `GetCurrentMedia_Link2` repeats the pattern at `0x167e54`. `DbcGetPlayerIDFromDeck` calls it at `0x168e5c` and reads `[r0, #0xd0]` at `0x168e78`. A total of 33 direct BL references were found by decoding ARM immediate branch targets.

Main agent reported the captured cell value `758635696`, which is `0x2d37dcb0`, consistent with a live heap pointer. This is not a screen ID and must not be logged or interpreted as an enum. This investigation does not establish the object's complete structure, size, lifetime, or a field that discriminates browser versus normal play. The table and next-step wording above have been corrected accordingly.
