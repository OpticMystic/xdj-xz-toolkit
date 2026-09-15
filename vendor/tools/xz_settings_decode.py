"""Decode XDJ-XZ /root/settings/XdjSettings.dat into JSON and Markdown."""

from __future__ import annotations

import argparse
import json
import pathlib
import struct


SIGNATURE = 0x2A7B9E8F
VERSION = 0x00010006
RECORD_SIZE = 0x8C


def boolean(value: int) -> bool:
    return bool(value)


# Names come from the v1.26 rbp Settings/StatWatcher getter symbols. Decode
# functions mirror the final enum normalization performed by those getters.
SETTING_DEFINITIONS = {
    0x60: ("load_lock", lambda v: boolean(v - 0x10), "Prevents track loading while playing"),
    0x61: ("needle_lock", lambda v: boolean(v - 0x20), "Needle-search lock"),
    0x62: ("quantize_value_code", lambda v: v - 0x30, "Firmware enum code"),
    0x63: ("hot_cue_auto_load", lambda v: boolean(v - 0x40), "Hot-cue auto load"),
    0x64: ("hot_cue_color", lambda v: boolean(v - 0x50), "Colored hot cues"),
    0x65: ("auto_cue_level_code", int, "Firmware auto-cue threshold enum"),
    0x66: ("slip_flashing", lambda v: boolean(v - 0x60), "Slip button flashing"),
    0x67: ("on_air_display", lambda v: boolean(v - 0x70), "On-air display"),
    0x68: ("vinyl_speed_adjust_mode_code", lambda v: v - 0x80, "Firmware enum code"),
    0x69: ("auto_play", lambda v: v == 0x91, "Auto-play enabled when enum is 0x91"),
    0x6A: ("jog_display_mode_code", lambda v: v - 0x130, "Firmware enum code"),
    0x6B: ("jog_ring_brightness", lambda v: v - 0x140, "Normalized brightness code"),
    0x6C: ("jog_ring_indicator", lambda v: boolean(v - 0x150), "Jog-ring indicator"),
    0x80: ("equalizer_curve_code", lambda v: v - 0xA0, "Firmware enum code"),
    0x81: ("channel_fader_curve_code", lambda v: v - 0xB0, "Firmware enum code"),
    0x82: ("master_attenuation_db", int, "dB"),
    0x83: ("booth_monitor_attenuation_db", int, "dB"),
    0x84: ("headphones_mono_split_code", lambda v: v - 0xC0, "Firmware enum code"),
    0x85: ("mixer_mode_code", lambda v: 1 if v == 0xD1 else 0, "Normalized mixer-mode code"),
    0x86: ("crossfader_curve_code", lambda v: v - 0x160, "Firmware enum code"),
    0x87: ("master_eq_enabled", lambda v: v != 0x170, "Master EQ"),
    0x88: ("peak_limiter", lambda v: boolean(v - 0x180), "Peak limiter"),
    0x89: ("mic_out_to_booth", lambda v: boolean(v - 0x190), "Microphone routed to booth"),
    0x8A: ("talk_over_level_db", int, "dB"),
    0x8B: ("talk_over_mode_code", lambda v: v - 0x1A0, "Firmware enum code"),
    0x8C: ("usb_output_level_db", int, "dB"),
    0x8D: ("channel_3_control_tone_code", lambda v: v - 0x1B0, "Firmware enum code"),
    0x8E: ("channel_4_control_tone_code", lambda v: v - 0x1C0, "Firmware enum code"),
    0x8F: ("mixer_midi_message_mode_code", lambda v: v - 0x1D0, "Firmware enum code"),
    0x90: ("language_code", int, "0 is the firmware default language"),
    0x91: ("lcd_brightness", int, "Brightness code"),
    0x92: ("screen_saver", lambda v: boolean(v - 0xE0), "Screen saver"),
    0x93: ("jog_lcd_brightness", int, "Brightness code"),
    0x94: ("pad_button_brightness", int, "Brightness code"),
    0xA0: ("auto_standby_code", lambda v: v - 0xF0, "Firmware enum code"),
    0x10000001: ("deck_1_auto_cue", boolean, "Per-deck state"),
    0x10000002: ("deck_2_auto_cue", boolean, "Per-deck state"),
    0x10000003: ("deck_1_reserved_mode", lambda v: v - 0x100, "Purpose not proven from symbols"),
    0x10000004: ("deck_2_reserved_mode", lambda v: v - 0x100, "Purpose not proven from symbols"),
    0x10000005: ("deck_1_quantize", boolean, "Per-deck state"),
    0x10000006: ("deck_2_quantize", boolean, "Per-deck state"),
    0x10000007: ("fx_quantize_mode_code", lambda v: v - 0x110, "Firmware enum code"),
    0x10000008: ("deck_1_vinyl_mode", boolean, "Per-deck state"),
    0x10000009: ("deck_2_vinyl_mode", boolean, "Per-deck state"),
    0x1000000A: ("deck_1_tempo_range_code", int, "2 corresponds to the current 16 percent range"),
    0x1000000B: ("deck_2_tempo_range_code", int, "2 corresponds to the current 16 percent range"),
    0x1000000C: ("deck_1_master_tempo", boolean, "Per-deck state"),
    0x1000000D: ("deck_2_master_tempo", boolean, "Per-deck state"),
    0x1000000E: ("deck_1_multi_pad_mode_code", int, "Firmware enum code"),
    0x1000000F: ("deck_2_multi_pad_mode_code", int, "Firmware enum code"),
}


def decode_settings(path: pathlib.Path) -> dict:
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError("Settings file is shorter than its header")
    signature, version = struct.unpack_from("<II", data)
    if signature != SIGNATURE:
        raise ValueError(f"Unexpected signature {signature:#010x}")
    if version != VERSION:
        raise ValueError(f"Unexpected version {version:#010x}")
    if (len(data) - 8) % RECORD_SIZE:
        raise ValueError("Settings payload is not an exact number of 140-byte records")

    settings = []
    for index in range((len(data) - 8) // RECORD_SIZE):
        offset = 8 + index * RECORD_SIZE
        key = struct.unpack_from("<I", data, offset)[0]
        raw_value = struct.unpack_from("<i", data, offset + 0x88)[0]
        definition = SETTING_DEFINITIONS.get(key)
        if definition:
            name, decoder, note = definition
            value = decoder(raw_value)
        else:
            name, value, note = f"unknown_{key:08x}", raw_value, "Unmapped key"
        settings.append(
            {
                "key": f"0x{key:08X}",
                "name": name,
                "value": value,
                "raw_value": raw_value,
                "raw_hex": f"0x{raw_value & 0xFFFFFFFF:08X}",
                "note": note,
            }
        )

    return {
        "source": str(path),
        "signature": f"0x{signature:08X}",
        "version": f"0x{version:08X}",
        "record_size": RECORD_SIZE,
        "record_count": len(settings),
        "settings": settings,
    }


def write_markdown(decoded: dict, path: pathlib.Path) -> None:
    lines = [
        "# XDJ-XZ Settings",
        "",
        f"- Signature: `{decoded['signature']}`",
        f"- Format version: `{decoded['version']}`",
        f"- Records: {decoded['record_count']}",
        "",
        "| Key | Setting | Interpreted value | Raw value | Note |",
        "|---|---|---:|---:|---|",
    ]
    for item in decoded["settings"]:
        value = json.dumps(item["value"]) if isinstance(item["value"], bool) else str(item["value"])
        lines.append(
            f"| `{item['key']}` | `{item['name']}` | {value} | "
            f"{item['raw_value']} (`{item['raw_hex']}`) | {item['note']} |"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("--output-dir", type=pathlib.Path)
    args = parser.parse_args()
    output_dir = args.output_dir or args.input.parent
    output_dir.mkdir(parents=True, exist_ok=True)
    decoded = decode_settings(args.input)
    json_path = output_dir / "XdjSettings.json"
    markdown_path = output_dir / "XdjSettings.md"
    json_path.write_text(json.dumps(decoded, indent=2) + "\n", encoding="utf-8")
    write_markdown(decoded, markdown_path)
    print(json_path.resolve())
    print(markdown_path.resolve())


if __name__ == "__main__":
    main()
