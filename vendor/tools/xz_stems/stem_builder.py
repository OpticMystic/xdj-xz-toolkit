"""XDJ-XZ Stems (.xzstem) Sidecar File Generator.

Generates 44.1kHz stereo phase-aligned sidecar stem payloads from audio stems.
Compatible with XDJ-XZ memory limits and real-time audio pipeline.
"""

import io
import os
import pathlib
import struct
import wave
from typing import Optional, Union

# Format 1 = float32 stereo, Format 2 = int16 stereo
FORMAT_FLOAT32 = 1
FORMAT_INT16 = 2
SAMPLE_RATE = 44100
HEADER_MAGIC = b"XZ3STM1\0"


def create_stem_payload(vocal_raw_pcm: bytes, sample_rate: int = 44100, is_float: bool = False) -> bytes:
    """Create .xzstem binary sidecar from raw stereo PCM samples."""
    bytes_per_sample = 4 if is_float else 2
    frame_size = bytes_per_sample * 2  # 2 channels (stereo)
    
    if len(vocal_raw_pcm) % frame_size != 0:
        vocal_raw_pcm = vocal_raw_pcm[: (len(vocal_raw_pcm) // frame_size) * frame_size]
        
    num_frames = len(vocal_raw_pcm) // frame_size
    fmt = FORMAT_FLOAT32 if is_float else FORMAT_INT16
    header_size = 32
    
    # 32-byte header:
    # 0..7: Magic "XZ3STM1\0"
    # 8..11: format (uint32)
    # 12..15: sample_rate (uint32)
    # 16..19: channels (uint32 = 2)
    # 20..23: header_size (uint32 = 32)
    # 24..27: frames (uint32)
    # 28..31: reserved (uint32 = 0)
    header = struct.pack(
        "<8sIIIIII",
        HEADER_MAGIC,
        fmt,
        sample_rate,
        2,
        header_size,
        num_frames,
        0
    )
    
    return header + vocal_raw_pcm


def write_stem_file(vocal_wav_path: Union[str, pathlib.Path], output_stem_path: Union[str, pathlib.Path]):
    """Convert a 16-bit stereo WAV into a high-performance .xzstem file."""
    with wave.open(str(vocal_wav_path), "rb") as wf:
        n_channels = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        framerate = wf.getframerate()
        n_frames = wf.getnframes()
        
        raw_frames = wf.readframes(n_frames)
        
    payload = create_stem_payload(raw_frames, sample_rate=framerate, is_float=(sampwidth == 4))
    pathlib.Path(output_stem_path).write_bytes(payload)
    return len(payload)
