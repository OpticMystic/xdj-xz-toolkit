# XZ 1.26 cue ABI evidence

Read-only analysis of SHA-256 6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2. ELF32 little-endian ARM ET_EXEC; .symtab 55,613 entries, .dynsym 523. Addresses below are virtual addresses, not file offsets. No hardware or writes tested.

## Exact useful symbols

- `_ZTVN2ui13PlayerInnardsE` 0x479080, size340. Itanium ARM address point at +8. Slot at 0x479090 stores 0x28e88c (`onPhysicalKey(IKeyInput const&)`). Other slots for onEv_PlayPause, onEv_CuePressed/Released, onEv_HotCue point to eight-byte base implementations, so hooking those is not equivalent to physical-pad interception.
- `_ZN2ui13PlayerInnards13onPhysicalKeyERKN3uif9IKeyInputE` 0x28e88c, size1412; tail branch 0x28eae4 to `onKey_Pad` 0x28d5cc; 0x28ed78 to `onKey_PlayPause` 0x28b4d4.
- `_ZN2ui13PlayerInnards9onKey_PadERKN3uif9IKeyInputE` 0x28d5cc, size4800. Input uint16 keycode at +8; pads checked 0x4119..0x4120. Input byte+0xb low nibble: operation0 inserts held-pad list node and marks byte this+0x8c+mappedpad; operations2 and3 remove list node; remaining operations return. Distinction 2 vs3 still needs runtime naming evidence.
- `_ZN2ui13PlayerInnards14execHotCuePlayEN8djengine9EnCueTypeE` 0x28f284, size200. Reads DjEngineIF pointer at this+0x30. Reads channel byte this+0x26 and passes engine channel `(byte == 2 ? 0 : 1)` by observed sub/rsbs/adc sequence. Calls DjEngineIF::playHotCue at0x432c4. Input blocking bytes +0x44/+0x45 checked. Return type not encoded in symbol; observed integer status used by caller.
- `_ZN8djengine10DjEngineIF10playHotCueENS_15EnPlayerChannelENS_9EnCueTypeEb` 0x432c4, size192.
- `_ZNK8djengine10DjEngineIF18isRegisteredHotCueENS_15EnPlayerChannelENS_9EnCueTypeE` 0x4343c.
- `_ZN8djengine10DjEngineIF11clearHotCueENS_15EnPlayerChannelENS_9EnCueTypeE` 0x43384.
- `_ZN8djengine10DjEngineIF14exitSlipHotCueENS_15EnPlayerChannelENS_9EnCueTypeE` 0x45d5c.
- `_ZN8djengine10DjEngineIF5pauseENS_15EnPlayerChannelEbbb` 0x3ff20; physical PlayPause calls with booleans true,true,false on a common playing path. Their meanings are not established.
- `onKey_HotCue` 0x28ca70 is the hotcue-mode button, not physical pad execution.

## Design comparison

1. LD_PRELOAD native-symbol interposition: rejected as sufficient interception. Cue/grid symbols are absent from .dynsym; dispatch is directly linked, demonstrated by branches above. Preload can still host a shim that uses separately validated native addresses.
2. Vtable hook of PlayerInnards::onPhysicalKey: viable first interception candidate. Use 32-bit expected-pointer replacement only after complete rbp hash/ABI gate, preserve original pointer, keep state per this/deck, restore on unload. Wrap original dispatch, observe input, avoid whole-object layouts. This catches existing routing without relocating ARM instruction prologues, but still needs runtime capture to prove actual dispatch reaches this slot and ensure GUI thread ownership.

No upstream EP122 cue offsets or operation words are portable. CDJ gate uses AsyncTask run slots, closure+0x28 pad, closure+0x2c release operation, and CueController facade; those class names are absent from XZ. XZ pad release already executes slip-exit and bookkeeping paths. Issuing arbitrary playHotCue/pause commands during release risks racing stock transport and is not a completed gate implementation.

## Minimal implementation strategy and hard gaps

First implement a hash-gated observer around the exact physical-key vtable slot, record deck/key/operation/mode and stock result. Then implement per-deck gate decisions using existing stock operation semantics after back-cue path is identified. Smart/preview add saved transport location and preview needle semantics; none can safely assume CDJ pointer chains. Must cover both decks, overlapping pads, delete/record pad, mode switches, slip, quantize, short release, Play latch, disable while held.

Grid native symbols exist (`GridAdjust`0xf3494, `GridAdjustReset`0xf35c0, `PlayerInnards::onEv_GridOffset(long)`0x288cd0); this establishes neither BPM double/half persistence nor upstream grid UI layout. Browse uses XZ-specific database and UI classes; upstream reorder specifically guards djdbSongPlaylist and must not mutate XZ playlist indices without a verified database contract.

Disassembly is reproducible via elf_inventory.py, disassemble.py, pads.py. They require pyelftools and capstone already available in this environment. Dumps include literal-pool words decoded as instructions; only bounded function/control-flow findings above were used.

## Reproduce the checked-in ABI manifest

Requires Python and pyelftools. From the repository root:

```powershell
python packages/xdj-xz-toolkit/mods/abi/inventory.py packages/xdj-xz-toolkit/vendor/decrypted_iso/pdj/extracted/pdj/rbp --output packages/xdj-xz-toolkit/mods/abi/xz-1.26-symbols.json
python packages/xdj-xz-toolkit/mods/abi/verify_inventory.py
```

The generator refuses an unknown full-file SHA-256 and never writes the rbp input. Layout assertions remain evidence from this exact binary, not a callable C++ header or proof of hardware behavior. The firmware input is not distributed with this manifest. Temporary disassembly scripts mentioned above are analysis scratch artifacts; the checked-in generator and verifier are the supported repeatable interface.

### Static vtable reachability proof

The PlayerInnards constructor at0x28a094 loads the literal word at0x28a360, value0x479088 (the ABI address point), then writes it at this+0 with the instruction at0x28a118. In `uif::PlayerSkeleton::onKey(IKeyInput const&)`, the path at0x255f6c obtains the innards instance, reads its vptr at0x255f80, reads vptr+8 at0x255f84, then calls it at0x255f88. This is exactly absolute slot0x479090. The caller compares integer return r0 to1 at0x255f9c and forwards the result enum to its post-handler. A C wrapper `int (*)(void *, const void *)` therefore preserves the observed call/return ABI; condition flags are caller-clobbered by AAPCS. Static reachability still needs a physical observer trace proving input reaches this path in the running target.
