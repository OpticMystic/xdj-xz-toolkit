# XZ player clock to Ableton Link

This computer-side bridge sends the tempo and four-beat bar phase of one
selected XZ player to the existing VJ.Tools Link daemon. Keep internal USB tracks
on XZ channels 1/2 and route your DJ software's decks to PC channels 3/4. Enable
Ableton Link in the software, then use its own deck sync controls.

The direction is **XZ → Link → software**. The bridge does not change XZ tempo,
select a Pioneer MASTER, control key, load tracks, or publish start/stop. It uses
Ethernet for timing and leaves the USB audio path unchanged. Stopping the bridge
or losing beat packets stops clock updates; the Link session keeps its last
tempo and its existing transport state.

## Run

Use Python 3.10 or newer and the existing compiled Link addon. From the repository
root, start the daemon in a terminal and leave its input open:

```powershell
& C:/Users/short/AppData/Local/pnpm/node.exe packages/link-daemon/daemon.cjs --port 17000
```

Connect the computer and XZ over Ethernet. Observe the selected player's beat
packets first; replace the example IP and device number with your setup:

```powershell
python packages/xdj-xz-toolkit/mods/sync/xz_to_link.py --source-ip 192.168.1.10 --device-number 1 --observe
```

Play an analyzed track. The console should report `observed`, its effective BPM
and beat 1 through 4. Remove `--observe` to publish this clock to Link:

```powershell
python packages/xdj-xz-toolkit/mods/sync/xz_to_link.py --source-ip 192.168.1.10 --device-number 1
```

Only the selected IP and player ID can drive Link. The bridge does not follow
master handoffs automatically. Choose the internal player whose clock the
computer should follow; sync the other internal player to it using the XZ.
Switch selection by stopping and restarting this bridge with the other ID.

UDP port 50001 must be available. The receiver does not share or take over an
occupied port, join as a virtual player, or send discovery/control packets.
If no beats reach the computer, it stays `waiting`; physical XZ Ethernet
delivery remains to be verified. Another Pro DJ Link listener may own this port.
Leave the existing VJ receiver running.

`--stale-seconds` defaults to 3.5; after that interval without valid selected
beats, the console reports `stale`. It resumes on a new valid beat. Malformed
packets, mixer clocks and other players cannot refresh the timeout. `--latency-ms`
accepts a measured signed adjustment: positive values advance the Link phase.
This compensates a measured offset, not variable network jitter.

## Validation and limits

```powershell
python -m unittest discover -s packages/xdj-xz-toolkit/mods/sync -v
$env:XZ_TEST_LINK_DAEMON='1'
$env:XZ_NODE='C:/Users/short/AppData/Local/pnpm/node.exe'
python -m unittest discover -s packages/xdj-xz-toolkit/mods/sync -v
```

The optional integration test starts a separate real native daemon on an
ephemeral TCP port, disables its Link participation before applying test values,
and checks tempo and phase. It preserves stdin because closing daemon stdin is
its normal shutdown signal. The fixture tests cover malformed input, pitch
scaling, source selection, duplicates, timeout/resume and occupied ports.

**Physical XZ + rekordbox/Serato acceptance is not complete.** Arrival time is
the phase reference; there is no hardware packet timestamp or measured audio
latency yet. Beat packets carry no sequence counter, so late out-of-order beats
cannot always be distinguished from musical jumps. Bar phase assumes the
analyzed grid describes four beats per bar. Test both internal decks, loops,
seeks, tempo changes and disconnects while recording mixed physical outputs
before relying on this for a performance. DJ software determines how its
playing decks respond to Link phase changes.

The bridge actively publishes tempo and forces bar-phase alignment on each
accepted beat. Other Link peers should follow this selected source; changing
tempo in another Link peer can compete with it. This is not bidirectional sync.

## Protocol evidence

`fixtures.json` contains constructed 96-byte packets, not device recordings.
Field offsets and validation are based on [Deep Symmetry's beat packet analysis](https://djl-analysis.deepsymmetry.org/djl-analysis/beats.html),
[Beat.java](https://github.com/Deep-Symmetry/beat-link/blob/main/src/main/java/org/deepsymmetry/beatlink/Beat.java)
and [Util.java](https://github.com/Deep-Symmetry/beat-link/blob/main/src/main/java/org/deepsymmetry/beatlink/Util.java).
No Java implementation was copied. The parser checks the ten-byte magic,
type 0x28, subtype, remaining length, repeated device ID, player ID range,
beat 1..4 and supported effective tempo. It reads the 24-bit pitch at 0x55 and
the hundredths-of-BPM field at 0x5a; effective BPM includes the pitch multiplier.

Pioneer's [official hybrid-source answer](https://community.pioneerdj.com/hc/en-us/community/posts/22978371623449-xdjxz)
describes the stock cross-source sync gap. The [local native ABI report](../../../../../.tmp/xz-extra-abi/hybrid-sync.md)
records additional XZ-specific timing seams; it is investigation output and may
not be present in a distributed package.
