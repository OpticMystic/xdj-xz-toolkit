# XZ native scrolling-wave ABI investigation

2026-09-08. Read-only static analysis of `.tmp/xz-port-live/rbp.running` and visual inspection of `artifacts/xdj-xz-mods/native-layout-usb-ready/screen.png`. No hardware mutations or source changes.

## Result

There is a concrete bounded presentation seam: the native application fully rebuilds one 536x268 RGB16 window containing both large scrolling waveforms before unlocking it. A transform immediately before that exact lock's unlock can compact the two lanes and draw stems in reclaimed space while retaining the native backing allocation. This avoids framebuffer feedback and leaves the native side labels, Beat FX column, track titles, counters and summary waveforms untouched.

This is a candidate implementation contract, not hardware acceptance. Window dimensions, drawing ranges and the synchronous lock/unlock path are established. Cross-thread native lifecycle serialization and every generic touch consumer are not proven. Use paired same-thread capture, validate exact current window and format, and skip a frame on any mismatch.

## Geometry and lifecycle

The actual screenshot is 800x480. The large scrolling-wave window occupies x131..666, y18..285. Native deck/source/key labels sit to its left; Beat FX sits at x690 onward. Track/counter panels start at y311. This agrees with the constructor, not an inferred blank region.

`ui_PLAYMODE_Set`, ELF VA `0x1fc2fc`, size6376, constructs the large window at `0x1fda00..0x1fda58`:

- `r0 = 0x01b31ed0 + 0xb8 = &wave_windowId`.
- `r1 = sp + 0x28`, descriptor pointer.
- descriptor +8 u16 width536, +10 u16 height268.
- descriptor +12 u32 native pixel-type selector9.
- descriptor +20 u32 zorder77.
- descriptor +28 i32 x131, +32 i32 y18.
- calls `DS_GR_CreateWindow` at `0x157344`, returns0 on success; result handle is stored through r0.

Exact standalone BSS symbols matter. The compiler uses one base for multiple neighboring objects; these are not all fields of the 64-byte `st_ui_playmode_wave_data` symbol:

| Address | Symbol | Meaning from use |
|---|---|---|
| `0x01b31ed0` | `st_ui_playmode_wave_data`,64B | two deck data records |
| `0x01b31f14` | `ExMyWaveTempData`,4B | current locked window pixel pointer |
| `0x01b31f4c` | `stCtrlVisible`,4B | play control visibility flag, base+0x7c |
| `0x01b31f50` | `bWaveInit`,4B | base+0x80 |
| `0x01b31f88` | `wave_windowId`,4B | GR window handle, base+0xb8 |

`ui_PLAYMODE_InitData` sets stCtrlVisible=1 at0x1fdcdc. `ui_PLAYMODE_Clear` clears it at0x1fdc58 and destroys wave_windowId when nonzero. `ui_PLAYMODE_Wave_Destory` at0x1fdd08 also destroys the handle and clears it on success. `ui_PLAYMODE_Set` can destroy/recreate the handle as modes change. `Ui_CautionIDCheck_Browse` at0xe1d44 is checked at0x1fd028; a nonzero result takes the destroy path. A live nonzero handle and flag are necessary, not sufficient, to assert that every visible scene is normal USB playback.

Do not use `uiPlaymodeWaveWinProperty` alone as geometry. Its two stored records are `{131,18,536,132,77}` and `{131,175,536,132,78}`, but the active combined-window constructor above explicitly uses536x268 at131,18. The old property record's second y does not match the combined buffer's placement.

## Complete native redraw and markers

- `0x1fd054`: `DS_GR_LockWindow(window, &pixels, &pitch)`.
- return address for that exact call is `0x1fd058`.
- native caller tests pixels nonnull; stores pixels to ExMyWaveTempData at0x1fd074.
- `0x1fd078`: clears exactly `0x46240` bytes, equal to536*268*2.
- subsequent drawing uses fixed row stride `0x430`, equal to1072 bytes.
- deck0 source lane is rows0..131; deck1 begins at row136 and ends at267, leaving4 separator rows.
- waveform, half-beat and cue-mark routines are called for each deck in the same draw pass.
- `ui_PlayMode_Wave_CopyCueBufferToWinBuffer` at0x1fa964 copies nonzero cue overlay pixels into each lane's top10 and bottom10 rows. Source references use row stride1072. The deck1 offset is136 rows. Preserve those10-row bands unscaled if readability matters; compress only the interior112 rows.
- native playhead lines are drawn into this same buffer before unlock at0x1fd3cc..0x1fd424. A transform of the completed window includes them.
- `0x1fd42c`: the only GR unlock call within this function, passing current wave_windowId.

Therefore do not allocate a smaller native backing surface or modify its width/height descriptor: the existing code writes a fixed287296 bytes and hardcoded row offsets. A smaller allocation risks an overwrite. Compact the completed pixels into the same original-size allocation with separate bounded scratch.

A possible geometry is two100-row lanes with4 separator rows, leaving64 rows for the strip inside the original268-row window. Preserve each lane's top10/bottom10 rows and resample only112 interior rows to80. This is a design suggestion, not a proven best layout. Keep x unchanged and test actual readability.

## Lock, pointer, pitch and format

`DS_GR_LockWindow` at0x156958 takes `(GRWindow *window, void **pixels, int *pitch)`. It passes window+0x1c to `DS_Task_LockWindow` without changing r1/r2. That task function calls `DS_MIF_LockWindow` directly; the latter tail-calls `DS_HW_LockWindow`. Despite the Task name, this particular path is synchronous, with no queue or worker handoff.

`DS_HW_LockWindow` at0x15cdbc follows HWwindow+0x98 to its core surface record, then +0x1c to the DirectFB surface interface. It invokes the surface Lock slot+0x34 with r1=2, r2=pixels, r3=pitch. These are the actual DirectFB output pointers. The GR wrapper marks flag0x01000000 and returns0 unconditionally after its task call. That zero alone does not prove the hardware lock succeeded. Require both a newly returned nonnull pixel pointer and exact pitch1072 before any transform. Clear observer outputs before calling stock if the wrapper owns those local outputs, or rely on the native caller's initialized null pointer without changing caller state.

Pixel type selector9 is proven by the create descriptor. `DS_HW_Core_Surface_Create` at0x159018 switches on native type; jump-table entry9 at0x159088 points to0x159160, which writes DirectFB format constant0x00200801 into the surface descriptor. This is the RGB16 format used by the existing receiver path. Native waveform drawing writes16-bit RGB565 colors, including0xffe0 playhead yellow. The proposed adapter should validate the live core/GR pixel-type field rather than assuming every window is RGB16.

GR window partial layout proven by constructor/getter/lock usage:

| Offset | Field |
|---:|---|
| +0 | u16 width |
| +2 | u16 height |
| +8 | u32 native pixel-type selector |
| +0x18 | flags; valid0x40000000, locked0x01000000 |
| +0x1c | underlying HW window handle |
| +0x30 | i32 x |
| +0x34 | i32 y |
| +0x38 | i32 x+width |
| +0x3c | i32 y+height |

Do not infer a complete struct from this partial map.

## Exact hook choices and return behavior

| Function | Address | First8 bytes | Notes |
|---|---:|---|---|
| DS_GR_LockWindow |0x156958|`38402de90040a0e1`|push/mov; copied8-byte prologue is PC-independent |
| DS_Task_UnlockWindow |0x1656c4|`10402de958d04de2`|push/sub; copied8-byte prologue is PC-independent |
| ui_PLAYMODE_Set |0x1fc2fc|`f04f2de9048b2ded`|push/vpush; PC-independent but a wrapper after return is too late to edit locked pixels |
| DS_GR_UnlockWindow |0x1569c0|starts PC-relative ldr|Do not use existing naive8-byte trampoline here |

`DS_Task_UnlockWindow` takes the underlying HW window handle, not the GR handle. It calls `DS_MIF_UnlockWindow`, which tail-calls `DS_HW_UnlockWindow`. HW unlock invokes the actual surface Unlock slot+0x3c. Normal success returns1 at this layer, while the GR wrapper discards that and returns0. Preserve the exact original task return in a hook. Transform before calling the task original, and never access pixels afterward.

Recommended capture protocol:

1. Wrap GR lock, call stock first and preserve its result. Capture only an exact native wave draw call, preferably identified by caller return0x1fd058 plus current wave_windowId identity, valid536x268/type9/position and newly returned pixel pointer/pitch1072.
2. Store the tuple `{GRhandle, HW handle, pixels, pitch, generation}` per thread. Use per-thread scratch or an explicit nonblocking ownership guard. Never global unguarded scratch.
3. At Task unlock, require same-thread captured tuple, matching HW argument, unchanged current wave_windowId and validated dimensions/type/locked flag. Only then transform before calling stock.
4. Clear captured state on every corresponding unlock, whether transformed or skipped. No pixels retained after unlock. New lock replaces stale capture; mismatches skip rendering.
5. Hooking only this exact paired caller avoids transforming summary waves, menus, arbitrary other windows, or partially redrawn surfaces.

The native locked flag itself is a plain non-atomic bit, not a proof of cross-thread lifecycle serialization. Static analysis establishes the synchronous call chain. It does not prove no other thread can destroy/recreate a window simultaneously. Same-thread capture plus identity/generation checks and no unlocked memory access constrain the adapter; actual concurrency/lifecycle tracing is still required before broad deployment.

## Touch relationships

The PlayMode touch constructor0x262f60 registers two deck-selection rectangles from rodata:

-0x476eb4: `{x0,y18,w115,h140}`.
-0x476ec4: `{x0,y161,w115,h140}`.

Both lie left of the large-wave window and can stay untouched.

The PlayInfo touch constructor0x2652ec registers summary needle-search rectangles `{10,414,300,61}` and `{412,414,300,61}`, plus time rectangles `{134,369,165,33}` and `{536,369,165,33}`. These lie below the scrolling-wave region and can stay untouched.

This does not prove the large wave has no native gesture. `TouchPanelHandler::solveCoordToKey` at0x2628b4 emits generic coordinate key0x23f for press/release/movement before its specialized touch-area dispatch. Its downstream generic consumers have not all been mapped here. Any new inline strip must consume only its owned gestures; transformed waveform contacts need a consistent inverse mapping before stock sees the generic event. Keep a contact's ownership and geometry fixed until release, including a drag leaving its original region. A stock contact outside this transform must retain its original coordinates.

Use piecewise inverse y mapping if preserving cue bands separately. Do not use a single linear mapping while rendering piecewise. Do not remap the left deck-selection region, Beat FX column, lower summary waves or time controls.

## Remaining acceptance

- Trace real lock/unlock tuple, format and pitch on this XZ, with two tracks and transitions to browser/settings/source changes.
- Verify the native window really disappears on browser and late unlock cannot restore a strip over another scene.
- Test malformed/mismatched pitch/handle/type and repeated lock without unlock in host fixtures; verify stock return values and no writes.
- Inspect actual compacted cue letters, beat lines and two playheads. Verify waveform/time remain current while using pads and touch.
- Confirm native touch behavior in compressed waveform and unchanged behavior in summary waves, times, labels and browser.

No full inline implementation or hardware acceptance claim is made by this report.

## Additional lifecycle proof: native callback arbitration

`ui_WS_BROWSER_OnUser` is the broad native scene controller despite its name. It chooses the active content controller through two exact tables, not GetGuiState:

- `ui_ListDispData` at0x03c6fc78 is12632B; u32 at+4, address0x03c6fc7c, selects a window-property record.
- `uiWindowData` at0x03eba538 is52B; +0x10 points to uiCtrlProperty, +0x14 points to uiWindowProperty.
- `ui_WS_BROWSER_OnInit` at0x1fe4f8 explicitly initializes these pointers at0x1fe530/0x1fe534.
- `uiCtrlProperty` at0x004d1f30 has11 records of20bytes. Record0 is `{id0, object0x100, active1, set0x001fc2fc, clear0x001fdbe4}`.
- `uiWindowProperty` at0x004d200c is896B,32 records of28bytes. Only record index2 has first word0, selecting controller0. Its seven u32 words are `{0,0xffffffff,0xffffffff,0xffffffff,1,0xffffffff,0xffffffff}`. All31 other records select other controller IDs.

At0x1fde84..0x1fdea0 the controller loads the selected index, multiplies it by28, compares that record's first word with each control record's ID. On equality it calls the control's Set callback at0x1fdee0. On inequality it hides the root object and calls Clear at0x1fde48. Therefore current index2 is the exact static discriminator for native play-controller execution. Validate the current table pointer equals0x004d200c and control callback pointers remain expected before relying on it. Main agent was asked to capture0x03c6fc7c on normal stock play and then browser. Do not call it a named enum until that physical observation.

This gives a grounded stronger gate for an opt-in native hook: requested stems enabled, full-screen MODS closed, selected native index2, expected table pointers/callbacks, stCtrlVisible nonzero, exact wave window handle and536x268/type9/position, and an exact same-thread paired lock/unlock with pitch1072. Set only publishes the compact geometry after a successful transformed frame. The touch path should revoke that geometry immediately when the current scene index no longer equals2, independently of whether another draw arrives.

Native cautions can still be drawn after the controller loop, and `ui_com_DrawCautionBrowse` explicitly destroys the wave window via0x14700c when appropriate. A changing scene index or missing window must immediately revoke touch ownership for NEW contacts. Existing owned releases must still be drained. Generic caution/status overlays beyond this waveform-destroying path and any modal menus should be covered in physical acceptance, not assumed absent.

The hook can be implemented as opt-in using the exact contracts above. Hardware activation should follow an observer trace confirming live index2, actual GR dimensions/type, output pitch1072 and expected paired call chain. Do not enable solely because the binary compiles.
