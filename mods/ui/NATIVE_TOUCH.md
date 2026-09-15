# Native XZ touch adapter evidence

Target only the exact XZ1.26 application hashes accepted by runtime. No installation or executable patching is performed by this adapter.

- `TouchPanelHandler::solveCoordToKey(TouchStatus const&,TouchPanelMode const&)` virtual address0x2628b4. Caller0x25efe8 ignores its return; use `void(void *self,const TouchStatus*,const void *mode)`.
- First8 bytes `f0452de90270a0e1`: ARM `push {r4,r5,r6,r7,r8,sl,lr}; mov r7,r2`. Neither is PC-relative or branches. A validated ARM trampoline may copy these then branch to0x2628bc. First16 bytes `f0452de90270a0e10030d1e514d04de2`.
- Direct BL caller0x25efe8 bytes `310e00eb` targets0x2628b4. Hooking the function return value as a supposed consume boolean cannot work.
- TouchStatus size12: downbyte0, unsignedx at4, unsignedy at8. Solver reads current status, compares down against self+4, and updates priorstatus by `ldm r5,{r0,r1,r2}; stmib r4,{r0,r1,r2}` at0x262958/0x26295c. Its activearea pointer is self+0x18; the adapter never writes it.
- `TouchPanel::commRxDataProc`0x25ef34 first16 `f0412de928d04de20040a0e120108de2`, calls readFd at0x25ef48, then calibration/hysteresis, then solver with self+0x354 and currentstatus self+0xdc. Raw6-byte ts_data has down0,x16at2,y16at4. Solver coordinates are already calibrated; receiver raw coordinates cannot be treated as screenpixels.

`native_touch.c` preserves stock input when hidden, captures the complete badge opening contact, routes visible touch into the standalone UI model, synthesizes a native release if stock had an active touch, and retains ownership through release after closing. The badge is x744,y0,w56,h24 by default; renderer must draw its visible MODS affordance in the same rectangle. Calls and visibility changes must occur on the touchthread or through an explicitly serialized queue. The apply callback should route `XZ_UI_CLOSE` to `xz_native_touch_visible(t,0)` and apply other actions through the native host.

Tests `test_native_touch.c` require active assertions. Host `-O2 -UNDEBUG` passes stock passthrough, openingcontact capture, synthesized stockrelease and closing without clickthrough. ARM32 soft-float compilation also passes. Actual device coordinate orientation, observer reachability and native overlay presentation remain hardware acceptance.

Physical MENU lead: old C `UiKey_Menu`0xdf1e8 accepts an unrelated native key record (reads words0 and8, longcounter>2 causes ChangeBrowseMode(6)). Its full record/keycode mapping is not proven. Do not cast that record to `uif::IKeyInput` or suppress the menu before proving the routing.
