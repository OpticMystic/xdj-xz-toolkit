# XZ key-shift native adapter

`runtime.c` hooks the exact XDJ-XZ 1.26
`dsp::TimeStretchManager::operate(long, Float2*, long, long)` entry at `0x76284`.
The first eight bytes are `f0412de90040a0e1`. The wrapper preserves all five
ARM32 arguments and returns the original source-position result unchanged.

The manager's active DSP pointer is at `+0x10`. The stock branch at
`0x76314..0x7631c` returns the input source position when that pointer is null
and does not write the output buffer. The adapter captures the active pointer
and published deck/rate/epoch before the original call, then requires the same
nonzero pointer and identity after it. It skips null-to-active, active-to-null,
engine-swap, and epoch-change transition blocks. Skipping a transition block is
safe; processing an uninitialized or ambiguously owned block is not.

`xz_audio_output_deck` recognizes only a manager captured by a successful
native load snapshot. It publishes manager, 44.1 kHz rate, and output epoch with
32-bit atomics. Load and unload increment the epoch before the original
operation, so lookup fails until the next complete snapshot. Runtime stop also
invalidates it. Stem enable and disable use a separate decode generation and do
not reset Key Shift. The key adapter reinitializes that deck's history when the
published output epoch changes.

Within one output epoch, the previous original return is the next expected
foreground source position. A mismatch resets that deck's history before
processing. Background/slip position changes alone do not reset it.

Control threads publish only an integer desired semitone. The audio callback
reads it once at the block boundary. Both desired values start at zero, and the
portable shifter keeps neutral output byte-identical. Start installs the hook;
stop disables processing and resets both desired values to zero. The code hook
stays mapped for the process lifetime under the parent runtime's nodelete rule.

The adapter does not implement Key Sync, track-key metadata, UI readiness, or
hook whitelisting in the parent runtime. Those are separate integration tasks.
