# Standalone XDJ-XZ mod port

Target: user-confirmed firmware 1.26. Load from USB at each boot. Preserve the
upstream stem files and interaction semantics, with controls fitted to 800x480.

Done means every feature in the upstream inventory runs on both XZ decks,
retains stock behavior when disabled, and passes physical audio/control/display
acceptance. Source, host tests, ARM build, package and hardware are separate gates.

## Work sequence

### Current acceptance, 2026-09-12 controls update

2026-09-13 follow-up: user confirms Beat Loop A/B/C mappings, reports no colour
feedback and no visible save message. Direct USB readback proves stems1,
smart1,stem_page1,pad_feedback1,shift_keysync0 were saved. Evidence:
artifacts/xdj-xz-mods/usb-settings-readback-20260913.json. Diagnostic connections
need explicit source169.254.168.58 when competing link-local routes exist;
device_smoke/live_io/restart shell now bind the known XZ adapter.
Save status now has its own line above the notice footer; regression checks
that a persistent notice cannot hide it. Diagnostic runtime a3dbf199c6c3 is
loaded as PID11989, receipt live-led-diagnostic-20260913.json. LED counters prove
the hook runs, Player/innards channels agree, and matching pad records exist;
live active STEMS/BeatLoop counter capture remains pending. No colour fix is
claimed. VJ output was paused again after restart. Saved-state restoration UI
and LED tests await user continuation; no firmware flash performed.

User confirmed Deck1 physical A/B/C mute and restore drums/harmonics/vocals.
Runtime e208f9780b06 is now loaded in rbp PID11311; receipt:
artifacts/xdj-xz-mods/live-config-led-session.json. It adds configurable pad
pages, optional Shift+mode-button stems, matching/dimmed native pad RGB, and
USB settings persistence. New physical mappings/LEDs and save/restart readback
await user checks. Isolated actual ARM decoder/settings/mix/decode and ten
library load tests passed before deployment; no firmware was flashed.

Preferences use VJ.Tools/XZ-Mods.cfg on the loader USB (XZ_MODS_USB).
Writes use a background worker, temporary file, fsync and rename. Invalid files
leave defaults; missing/unmounted volumes report a save error. Requested stems
enablement survives unavailable audio. Shift+Sync is off by default; its input
is decoded, but automatic Key Sync remains unavailable until musical key and
master metadata are qualified. It currently reports that missing prerequisite.

Native-style controls are live in MODS. The compact main-wave transform has
portable ownership/geometry/transaction tests but its drawing hooks are not
installed; do not call the main screen inline integration complete.

Independent builder now imports DJ branding to immutable versioned bundles in
VJ.Tools/Branding. The documented manifest enables future VJ.Tools/Wingman
discovery; network retrieval consumers are not implemented. All30 builder tests
pass, including native cache/FLAC and real branding copies. New builder UI and
backend source have not been repackaged in the older preview ZIP.

VJ.Tools output is temporarily paused via the existing preview state event;
after XZ testing restore previewOutput.setMode('xdj-xz-filmstrip'). Restarting
rbp caused a reconnect once and required pausing again. The permanent Library
disable fix remains in an isolated patch, not installed.

- [x] Read workflow principles and current coordination.
- [x] Inventory upstream features and inspect the local firmware ELF.
- [ ] Ground exact XZ cue, UI, browser, grid and audio ABIs.
- [x] Compare native hook designs and select the smallest verified adapter.
- [x] Establish repeatable binary, cache and cross-build checks.
- [x] Port upstream cache compatibility and portable behavior.
- [ ] Connect native cue/grid/browser/settings/theme controls.
- [ ] Connect native stem mixing, waveform, Groove Circuit and X-PAD.
- [x] Package private observer and experimental RAM boot images.
- [x] Package a separate reusable VJ.Tools receiver integration and test staging on XZ.
- [ ] Verify physical two-deck playback, controls and display.
- [ ] Review the implementation and decision log against the complete inventory.

The upstream snapshot is e74e199603e2a25567950ca72997c38d17ada4e8.
No missing device hook may be replaced with a no-op and reported as implemented.
The deployed working VJ receiver remains unchanged. A separate optional display
bridge was added to source for the experimental combined image. Its null-callback
behavior and protocol were checked against the existing receiver tests.

## Current physical gate

The two private images were built and checked by decrypting and reading their
contents back. The paired libraries loaded ten times in isolated XZ processes.
Cue, residual mixing and real WAV/FLAC decode tests passed on the XZ. The running
DJ process stayed at PID 15648 throughout these checks.

The user authorized a restart and chose stems first. Experimental runtime
0f95f510db9a and its display bridge were loaded into rbp PID 415. Physical touch
opened the panel. The compatible test cache loaded, and the user confirmed
vocal removal, bypass restoration, and drum removal on the brighter fixture.

The reported fader problem was a listening mix-up. The user confirmed the
fader was working; loud headphones had been audible over the main speakers.
No fader change is required. Experimental runtime 0f95 was restored as PID
1488 and its loaded mapping was verified again before pad integration.

This is not a completed full port or a public firmware release. The private
boot images have not undergone a fresh-boot acceptance run.

The user added key shift/key sync and a hybrid four-deck target: two native decks
plus two computer decks over USB, with Serato or rekordbox. The software clock
bridge currently follows a selected XZ player in one direction. It is not a
bidirectional master-handoff implementation.

## Pad integration checkpoint

Runtime 28289d9c6dd9 passed host UI/pad/touch/cue/key tests, receiver checks,
and ten isolated actual-XZ loads plus native cue/mix/decoder tests. It was
loaded as PID 1757 using the reviewed private image and the matching previous
receipt. New mappings and library hash were read back. Physical pad presses
and stock USB screen integration remain unverified. The framebuffer immediately
after restart showed a black background plus MODS, so stock layout geometry
is still unavailable. See artifacts/xdj-xz-mods/live-pads-session.json.

## Inline stems preparation

Actual stock USB screen captured at native-layout-usb-ready. Native play property
index2 and table pointer0x4d200c read back on PID1757. NATIVE_WAVE_ABI.md records
the native536x268 locked redraw seam. Portable strip renderer, shared control
actions, region-owned touch, and two-lane compaction preserving cue bands pass
host tests and ARM compilation. No new native wave hook is installed. Live
remains pad build28289. Browser/modal transitions and actual lock tuple
qualification must precede enabling the compact view.

## Physical pad failure and correction

The user confirmed Deck1 was selected during the failed test. A bounded
in-memory trace proved live native channels1/2. Constructor and ARM carry
analysis confirmed channel1 maps engine0 and channel2 maps engine1; the previous
2/3 decoder rejected the left deck and misrouted the right. The actual ARM
decoder regression fails before the correction and passes after it. The
corrected0187d4c8 build passed all isolated XZ tests including the new mandatory
native decoder test. Physical audible retest remains pending.

## Offline native-style UI, 2026-09-12

The user is using VJ.Tools and asked for development without interruptions or
hands-on tests. The live pad fix remains unaccepted by listening. No device
operations were performed after that request. Native-style C UI rendering is
implemented locally with an antialiased atlas, native deck outlines and orange
track header, clearer mute/read-only/unavailable states, and all themes retained.
Local tests, reproducible previews and ARM ABI build passed. Runtime build
2b52d429e405 remains local, not loaded. Preview: artifacts/xdj-xz-mods/
native-style-20260912/index.html. Native inline hooks and physical acceptance
remain outstanding.
