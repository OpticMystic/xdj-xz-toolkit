# XZ physical pad event ABI check

2026-09-08. Read-only analysis of `.tmp/xz-port-live/rbp.running`, current cue/native.c and runtime.c. No source edits or device mutations.

## Result

Concrete decoder mismatch confirmed after live trace: internal UI channels are1 and2, mapping respectively to engine decks0 and1. The adapter accepting2/3 and mapping2 to deck0 is incorrect. Native key ID, operation, HOT CUE mode and vtable slot remain supported by the evidence below.

## Vtable and upstream dispatch

`_ZTVN2ui13PlayerInnardsE` begins at0x00479080. Its address point is0x00479088. The two destructor slots precede onPhysicalKey, so the method offset is+8 from the address point:

```text
0x479088 -> 0x28f108 PlayerInnards::~PlayerInnards
0x47908c -> 0x28f1ec deleting destructor
0x479090 -> 0x28e88c PlayerInnards::onPhysicalKey
0x479094 -> 0x289308 PlayerInnards::isIgnoreLockKey
```

`Player::asEventCode` at0x27bbe0 returns event code1 for key0x4119..0x4120. Its only exceptions are0x4324,0x4325,0x4126,0x4127. It reads key as u16 input+8.

`PlayerSkeleton::onKey` at0x255e98 calls this classifier. For event1, at0x255f6c..0x255f88 it calls getInnards, then loads the innards vptr, then method+8 and BLX. This resolves precisely to slot0x479090 for a PlayerInnards instance. `Player::getInnards` at0x27b9d0 returns the pointer stored in Player+0x138.

No direct ARM B/BL references to onPhysicalKey0x28e88c exist in this binary; the identified caller is virtual. One direct reference to onKey_Pad0x28d5cc exists: the tail branch at0x28eae4 from onPhysicalKey.

The PlayerSkeleton path checks `IKeyInput::isPlayerKey` through input vtable+0x38 first and has lock handling before dispatch. Other controllers, PC routing, or an earlier event consumer can prevent the event reaching PlayerInnards. The vtable slot being patched successfully is not evidence that a physical event entered it.

## Pad key numbers

`PlayerInnards::onPhysicalKey` ultimately tail-branches at0x28eae4 to `onKey_Pad` at0x28d5cc for pad keys.

`onKey_Pad` starts:

```text
0x28d5cc ldrh r3,[r1,#8]
0x28d5d4 sub r3,r3,#0x4100
0x28d5d8 sub r3,r3,#0x19
0x28d5e0 cmp r3,#7
0x28d5ec movhi r0,#0
0x28d5f0 bhi return
```

Thus accepted native IDs are exactly0x4119..0x4120. This supports pad indices0..7. There is an additional native lookup table from raw pad index to logical pad; its entries should be logged/checked if the wrong stem toggles, but cannot explain no hook entry. The current UI remap intentionally operates directly on physical A-D before stock cue dispatch.

## Operations

`onKey_Pad` at0x28d614 loads input byte+11; at0x28d618 masks low nibble. Operation0 allocates/inserts the active pressed pad into native list at self+0x84. Nonzero operations go to0x28d854, subtract2 and accept values0 or1 only, meaning original operations2 and3. That path removes the active pad from its list. Other operation values return without action.

This proves current0 press and2/3 release handling. Do not reinterpret operation1 as a normal pad press without live evidence.

## HOT CUE mode and deck

Mode is u32 self+0x80. `onKey_Pad` uses it as a jump-table index at0x28d654..0x28d674. Mode0 jumps to0x28d8cc, which invokes registered-hot-cue checks and ultimately execHotCuePlay0x28f284. Native HOT CUE mode key0x4114 is handled at0x28eca4. On operation0 with no held pad, it stores0 to self+0x80 at0x28ecd4. Other physical mode keys store2,3,4 etc. Thus word(self+0x80)==0 is the correct HOT CUE mode gate.

The channel is the byte at self+0x26. PlayerInnards constructor0x28a094 forwards its channel argument to UiObject constructor0x25765c, which stores that byte at0x257678. Native pad code0x28d8cc..0x28d8d8 computes engine index1 exactly when channel==2, otherwise0. The earlier report reversed the carry semantics of RSBS/ADC. UiObjectManager::init constructs Player channels1 and2 explicitly, so the adapter must accept1/2 and map1->deck0,2->deck1. Accepting2/3 both rejects the left deck and misroutes the right deck.

Engine interface is self+0x30, used repeatedly by the native pad handler. None of these offsets shifted in the current inspected binary.

## Diagnostics that distinguish the fault

Count/log physical_key hook entry BEFORE xz_cue_native_decode; logging only decoded pad events hides failures in channel validation or key classification. For a bounded sample record:

- self and its actual vptr; current word at patched slot0x479090.
- self channel byte+0x26 and mode word+0x80.
- raw key u16 input+8, byte input+10 and raw operation byte input+11.
- decoded deck,pad,operation,HOT CUE flag.
- UI started/visible/tab/selected deck, stems enabled, audio readiness and mapping enabled.
- UI pad consumed result and stock result when forwarded.

Read input bytes only after validating nonnull input; do not diagnose by calling stock twice. Preserve exact return and paired release consumption. The mode prerequisite is physical HOT CUE mode, which is independent of selecting Deck1 in the MODS panel.

If hook entry stays zero while native hot cues respond, the next bounded observer should trace PlayerSkeleton::onKey or native onKey_Pad and inspect the returned actual innards vptr. If hook entry appears but decoding rejects, fix only the field evidenced by raw data. If decoding succeeds, the failure is downstream UI/readiness/mute-state routing, not these ABI constants.

The incorrect channel mapping is a concrete root cause of rejected left pad events and potential right-to-left routing. Physical confirmation after correction remains required.

## Corrected ARM carry proof and constructors

The live trace reported channels1 and2. Re-evaluation of the exact native instructions proves the mapping:

```text
0x28d8cc ldrb sb,[r4,#0x26]
0x28d8d0 sub r3,sb,#2
0x28d8d4 rsbs sb,r3,#0
0x28d8d8 adc sb,sb,r3
```

For channel1, subtraction gives0xffffffff. RSBS computes0-0xffffffff=1 with borrow, so carry0; ADC computes1+0xffffffff+0=0 modulo2^32. For channel2, subtraction gives0, RSBS computes0 with no borrow, so carry1; ADC computes0+0+1=1. This is the ARM boolean idiom `(channel == 2)`, not its inverse. Master-tempo path0x28eb78..0x28eb84 independently repeats the same conversion before passing engine index in r1 to DjEngineIF::isMasterTempo.

UiObjectManager::init explicitly calls Player::Player at0x293d44 with r1=1, then again at0x293d7c with r1=2. Player constructor passes its channel through to PlayerInnards and UiObject, whose byte store at+0x26 was already verified. This independently proves actual internal Player channel values1/2; the trace is consistent with the binary.

Required source correction: accept only byte1 or2; compute deck=channel-1. Keep invalid-channel refusal for other values. Update native decoder fixtures to exercise both channels and reject0/3. Search other newly added native adapters for the same2/3 assumption. Do not change correct protocol player IDs or engine deck indices merely because they also contain1/2 or0/1.
