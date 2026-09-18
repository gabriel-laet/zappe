#!/usr/bin/env python3
"""Press-each-button capture for ATONGX / XING WEI (and the next cheap remote).

Reads /proc/bus/input/devices KEY bitfields, then listens on matching evdev
nodes and prints EV_KEY (+ MSC_SCAN) lines. Annotates actions from
src/lib/atongx-map.json when the file is present.

No third-party modules — Python 3 stdlib only.
"""

from __future__ import annotations

import argparse
import json
import os
import select
import struct
import sys
import time
from pathlib import Path

EV_SYN = 0
EV_KEY = 1
EV_REL = 2
EV_MSC = 4
MSC_SCAN = 4

# struct input_event on 64-bit Linux: timeval (2 × long) + type/code/value
EVENT_FMT = "llHHi"
EVENT_SIZE = struct.calcsize(EVENT_FMT)

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
DEFAULT_MAP = REPO / "src" / "lib" / "atongx-map.json"

KEY_NAMES = {
    1: "KEY_ESC",
    14: "KEY_BACKSPACE",
    28: "KEY_ENTER",
    57: "KEY_SPACE",
    59: "KEY_F1",
    60: "KEY_F2",
    64: "KEY_F6",
    65: "KEY_F7",
    66: "KEY_F8",
    67: "KEY_F9",
    68: "KEY_F10",
    102: "KEY_HOME",
    103: "KEY_UP",
    104: "KEY_PAGEUP",
    105: "KEY_LEFT",
    106: "KEY_RIGHT",
    108: "KEY_DOWN",
    109: "KEY_PAGEDOWN",
    111: "KEY_DELETE",
    113: "KEY_MUTE",
    114: "KEY_VOLUMEDOWN",
    115: "KEY_VOLUMEUP",
    116: "KEY_POWER",
    127: "KEY_COMPOSE",
    139: "KEY_MENU",
    142: "KEY_SLEEP",
    158: "KEY_BACK",
    164: "KEY_PLAYPAUSE",
    172: "KEY_HOMEPAGE",
    207: "KEY_PLAY",
    217: "KEY_SEARCH",
    272: "BTN_LEFT",
    273: "BTN_RIGHT",
    274: "BTN_MIDDLE",
    352: "KEY_OK",
    353: "KEY_SELECT",
    398: "KEY_RED",
    438: "KEY_CONTEXT_MENU",
    582: "KEY_VOICECOMMAND",
}


def load_map(path: Path) -> dict:
    if not path.is_file():
        return {}
    with path.open(encoding="utf-8") as fh:
        return json.load(fh)


def action_for_code(spec: dict, code: int) -> tuple[str | None, str | None]:
    for binding in spec.get("bindings", []):
        for key in binding.get("keys", []):
            if int(key.get("code", -1)) == code:
                return binding.get("action"), key.get("name")
    return None, None


def key_name(code: int, spec: dict) -> str:
    _, mapped = action_for_code(spec, code)
    if mapped:
        return mapped
    return KEY_NAMES.get(code, f"KEY_{code}")


def parse_devices(text: str) -> list[dict]:
    blocks = []
    cur: dict = {"props": {}, "bits": {}, "handlers": []}
    for raw in text.splitlines() + [""]:
        line = raw.rstrip()
        if line.startswith("#"):
            continue
        if not line:
            if cur["props"] or cur["bits"]:
                handlers = cur["props"].get("H", "")
                cur["handlers"] = handlers.split()
                ev = next((h for h in cur["handlers"] if h.startswith("event")), None)
                cur["event"] = ev
                cur["path"] = f"/dev/input/{ev}" if ev else None
                blocks.append(cur)
            cur = {"props": {}, "bits": {}, "handlers": []}
            continue
        if len(line) < 3 or line[1] != ":":
            continue
        kind, rest = line[0], line[2:].strip()
        if kind == "B":
            if "=" in rest:
                name, hexwords = rest.split("=", 1)
                cur["bits"][name.strip()] = hexwords.split()
        else:
            cur["props"][kind] = rest
    return blocks


def decode_bitfield(hex_words: list[str], bits_per_long: int | None = None) -> list[int]:
    """Decode a /proc/bus/input/devices B: KEY= bitfield.

    The kernel prints unsigned longs, high word first, without padding.
    zappe-tv is x86_64 (64-bit longs). A 32-bit dump usually has ~20+ words
    for KEY_MAX; we only switch if the caller asks or the dump is that wide.
    """
    if not hex_words:
        return []
    if bits_per_long is None:
        widest = max(len(w) for w in hex_words)
        if widest > 8:
            bits_per_long = 64
        elif len(hex_words) >= 20:
            bits_per_long = 32
        else:
            bits_per_long = 64
    words = [int(w, 16) for w in hex_words]
    words.reverse()
    codes = []
    for index, word in enumerate(words):
        for bit in range(bits_per_long):
            if word & (1 << bit):
                codes.append(index * bits_per_long + bit)
    return codes


def match_name(name: str, spec: dict) -> bool:
    needles = spec.get("device", {}).get("matchNames") or ["XING WEI", "ATONGX"]
    upper = name.upper()
    return any(n.upper() in upper for n in needles)


def usb_ids(block: dict) -> tuple[str | None, str | None]:
    ident = block["props"].get("I", "")
    vendor = product = None
    for part in ident.split():
        if part.startswith("Vendor="):
            vendor = part.split("=", 1)[1].lower()
        if part.startswith("Product="):
            product = part.split("=", 1)[1].lower()
    return vendor, product


def is_remote(block: dict, spec: dict) -> bool:
    name = block["props"].get("N", "").strip().strip('"')
    if match_name(name, spec):
        return True
    vendor, product = usb_ids(block)
    usb = spec.get("device", {}).get("usb") or {}
    if vendor and product and vendor == usb.get("vendor", "").lower() and product == usb.get(
        "product", ""
    ).lower():
        return True
    return False


def iface_of(block: dict) -> str:
    name = block["props"].get("N", "")
    sysfs = block["props"].get("S", "")
    handlers = " ".join(block.get("handlers", []))
    if "Consumer Control" in name or "event-if03" in sysfs:
        return "consumer"
    if "System Control" in name:
        return "system"
    if "mouse" in handlers or "event-mouse" in sysfs:
        return "mouse"
    if "kbd" in handlers or "event-kbd" in sysfs:
        return "keyboard"
    keys = set(decode_bitfield(block["bits"].get("KEY", [])))
    if {158, 172, 164} & keys:
        return "consumer"
    if 116 in keys and 28 not in keys:
        return "system"
    if "REL" in block["bits"] and 103 not in keys:
        return "mouse"
    return "keyboard"


def print_inventory(blocks: list[dict], spec: dict) -> list[dict]:
    remotes = [b for b in blocks if is_remote(b, spec)]
    if not remotes:
        print("No XING WEI / ATONGX node in /proc/bus/input/devices.", file=sys.stderr)
        print("Plug the dongle, then: ls -l /dev/input/by-id | grep -i xing", file=sys.stderr)
        return []
    print("# ATONGX / XING WEI inventory")
    print(f"# map: {DEFAULT_MAP if DEFAULT_MAP.is_file() else '(missing)'}")
    print()
    for block in remotes:
        name = block["props"].get("N", "").strip()
        ident = block["props"].get("I", "")
        sysfs = block["props"].get("S", "")
        iface = iface_of(block)
        print(f"## {block.get('event')}  {name}  ({iface})")
        print(f"   path: {block.get('path')}")
        print(f"   {ident}")
        if sysfs:
            print(f"   {sysfs}")
        keys = decode_bitfield(block["bits"].get("KEY", []))
        if keys:
            named = " ".join(key_name(c, spec) for c in keys if c < 768)
            print(f"   KEY bits ({len(keys)}): {named}")
        print()
    return remotes


def listen(blocks: list[dict], spec: dict, prompt: bool) -> int:
    targets = [b for b in blocks if b.get("path") and iface_of(b) != "mouse"]
    fds: dict[int, dict] = {}
    for block in targets:
        path = block["path"]
        try:
            fh = open(path, "rb")
        except OSError as err:
            print(f"# cannot open {path}: {err}  (add user to group input?)", file=sys.stderr)
            continue
        os.set_blocking(fh.fileno(), False)
        fds[fh.fileno()] = {"fh": fh, "block": block}
    if not fds:
        print("No readable evdev nodes. Try: sudo usermod -aG input \"$USER\"", file=sys.stderr)
        return 2

    bindings = spec.get("bindings") or []
    remaining = [b.get("button") for b in bindings] if prompt else []
    if prompt and remaining:
        print("# Walk each physical button. Ctrl-C ends.")
        print(f"# next → {remaining[0]}")
    else:
        print("# Listening. Press each button. Ctrl-C ends.")

    pending_scan = None
    try:
        while True:
            ready, _, _ = select.select(list(fds), [], [], 0.25)
            for fd in ready:
                meta = fds[fd]
                data = meta["fh"].read(EVENT_SIZE)
                if not data or len(data) < EVENT_SIZE:
                    continue
                _sec, _usec, etype, code, value = struct.unpack(EVENT_FMT, data)
                if etype == EV_MSC and code == MSC_SCAN:
                    pending_scan = value
                    continue
                if etype != EV_KEY:
                    continue
                if value == 2:
                    continue
                evname = key_name(code, spec)
                action, _ = action_for_code(spec, code)
                state = {0: "release", 1: "press"}.get(value, str(value))
                iface = iface_of(meta["block"])
                extra = f"  → {action}" if action else "  → (unmapped)"
                scan = f"  MSC_SCAN 0x{pending_scan:08x}" if pending_scan is not None else ""
                pending_scan = None
                stamp = time.strftime("%H:%M:%S")
                print(
                    f"[{stamp}] {meta['block'].get('event')} {iface:9} "
                    f"{evname} ({code}) {state}{extra}{scan}",
                    flush=True,
                )
                if prompt and remaining and value == 1:
                    got = remaining.pop(0)
                    print(f"# recorded {got!r} as {evname} ({code}) on {iface}", flush=True)
                    if remaining:
                        print(f"# next → {remaining[0]}", flush=True)
                    else:
                        print("# done — every mapped button was pressed once.", flush=True)
                        return 0
    except KeyboardInterrupt:
        print("\n# stopped")
        return 0
    finally:
        for meta in fds.values():
            meta["fh"].close()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, default=DEFAULT_MAP)
    parser.add_argument("--list", action="store_true", help="print KEY bitfields and exit")
    parser.add_argument(
        "--listen",
        action="store_true",
        help="print every EV_KEY (default when not --list / --prompt)",
    )
    parser.add_argument(
        "--prompt",
        action="store_true",
        help="ask for each button in atongx-map.json, in order",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="do not filter by XING WEI / ATONGX name (future remotes)",
    )
    parser.add_argument(
        "--devices-file",
        type=Path,
        default=Path("/proc/bus/input/devices"),
        help="override /proc/bus/input/devices (tests / saved dumps)",
    )
    args = parser.parse_args()

    spec = load_map(args.map)
    try:
        text = args.devices_file.read_text(encoding="utf-8", errors="replace")
    except OSError as err:
        print(f"cannot read {args.devices_file}: {err}", file=sys.stderr)
        return 2

    blocks = parse_devices(text)
    chosen = blocks if args.all else [b for b in blocks if is_remote(b, spec)]
    if args.all and not chosen:
        chosen = blocks
    inventory = print_inventory(chosen if chosen else blocks, spec)
    if args.list:
        return 0 if inventory else 2
    if not inventory and not args.all:
        return 2
    if not args.list:
        return listen(inventory or chosen, spec, prompt=args.prompt)
    return 0


if __name__ == "__main__":
    sys.exit(main())
