#!/usr/bin/env python3
"""
League of Legends *.bin* quick viewer.

Handles:
• Zstandard-compressed files straight from *.wad.client
• Optional PTCH wrapper header
• PROP v1-v4 files (communitydragon spec)
"""

from __future__ import annotations
from pathlib import Path
import struct, io, json, argparse, zstandard as zstd
from hash_unhasher import RiotHashUnhasher  # your helper

# --------------------------------------------------------------------------- #
TYPE_NAMES = {
    0:"NONE",1:"BOOL",2:"I8",3:"U8",4:"I16",5:"U16",6:"I32",7:"U32",8:"I64",9:"U64",
    10:"F32",11:"VEC2",12:"VEC3",13:"VEC4",14:"MAT4",15:"RGBA",
    16:"STRING",17:"HASH",18:"ARRAY",19:"STRUCT",20:"EMBED",
    21:"LINK",22:"OPTION",23:"MAP",24:"FLAG"
}
ZSTD_MAGIC = b"\x28\xB5\x2F\xFD"

def _maybe_decompress(raw: bytes) -> bytes:
    return zstd.ZstdDecompressor().decompress(raw) if raw.startswith(ZSTD_MAGIC) else raw

def _read_cstring(buf: io.BytesIO) -> str:
    out = bytearray()
    while (c := buf.read(1)) and c != b"\x00":
        out.extend(c)
    return out.decode("utf-8", errors="replace")

# --------------------------------------------------------------------------- #
def parse_bin(path: Path, unhasher: RiotHashUnhasher) -> dict:
    buf = io.BytesIO(_maybe_decompress(path.read_bytes()))

    # ── optional PTCH header (12 bytes after “PTCH”) ────────────────────────
    if buf.read(4) == b"PTCH":
        buf.seek(12)
    else:
        buf.seek(0)

    if buf.read(4) != b"PROP":
        raise ValueError(f"{path.name}: missing PROP header")

    # Peek at the next two u32s and decide which is version vs entryCount
    a, b = struct.unpack("<II", buf.read(8))
    if a <= 10:                 # very small → definitely the version
        version, entry_cnt = a, b
    elif b <= 10:
        version, entry_cnt = b, a
    else:
        raise ValueError("Unable to locate version field (neither int ≤10)")

    # Sanity-check entry_cnt against file size
    remaining = len(buf.getbuffer()) - buf.tell()
    if entry_cnt * 4 > remaining:
        raise ValueError(f"entry_cnt={entry_cnt} is impossible for file size {remaining}B")

    # ── linked files (only when version ≥2) ────────────────────────────────
    links = []
    if version >= 2:
        (link_cnt,) = struct.unpack("<I", buf.read(4))
        links = [_read_cstring(buf) for _ in range(link_cnt)]

    # ── entry-type hash table (1 u32 per entry) ────────────────────────────
    entry_type_hashes = struct.unpack(f"<{entry_cnt}I", buf.read(4 * entry_cnt))

    # ── entries -----------------------------------------------------------
    entries = []
    for idx in range(entry_cnt):
        entry_start = buf.tell()
        entry_len, entry_hash, field_cnt = struct.unpack("<IIH", buf.read(10))

        fields = []
        for _ in range(field_cnt):
            (field_hash,) = struct.unpack("<I", buf.read(4))
            type_id       = buf.read(1)[0]
            fields.append({
                "field" : unhasher.unhash(field_hash),
                "typeId": type_id,
                "type"  : TYPE_NAMES.get(type_id, f"UNKNOWN({type_id})")
            })

        buf.seek(entry_start + entry_len)   # skip value blob

        entries.append({
            "entry"    : unhasher.unhash(entry_hash),
            "typeHash" : entry_type_hashes[idx],
            "fieldCnt" : field_cnt,
            "fields"   : fields
        })

    return {
        "file"   : path.name,
        "version": version,
        "links"  : links,
        "entries": entries
    }

# --------------------------------------------------------------------------- #
def _main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("binfile", type=Path)
    ap.add_argument("-o", "--out", metavar="dump.json", help="write JSON dump")
    args = ap.parse_args()

    unhasher = RiotHashUnhasher("tools/hashtable/hashes/lol")
    parsed   = parse_bin(args.binfile, unhasher)

    print(json.dumps(parsed, indent=2, ensure_ascii=False))
    if args.out:
        Path(args.out).write_text(json.dumps(parsed, indent=2, ensure_ascii=False))
        print(f"[✓] JSON written → {args.out}")

if __name__ == "__main__":
    _main()
