#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ritobin_mac.py – step-by-step port of RitoBin’s binary reader
Currently parses the PROP / PTCH header, linked list, and the root “entries” map.

Usage:
    python3 ritobin_mac.py <file.bin>
"""

# ---------------------------------------------------------------------------
# 0. standard-library imports
# ---------------------------------------------------------------------------
import sys
import struct
import pprint
import datetime
from enum import IntEnum
from pathlib import Path
from typing import Any, Dict, List


# ---------------------------------------------------------------------------
# 1. tiny helper – read the whole file
# ---------------------------------------------------------------------------
def read_bin_file(path: Path) -> bytes:
    with path.open("rb") as f:
        buf = f.read()
    print(f"[info] read {len(buf):,} bytes from {path.name}")
    return buf


# ---------------------------------------------------------------------------
# 2. enum Type  (numeric values copied from bin_types.hpp)
# ---------------------------------------------------------------------------
class Type(IntEnum):
    BOOL    = 1
    I8      = 2
    U8      = 3
    I16     = 4
    U16     = 5
    I32     = 6
    U32     = 7
    I64     = 8
    U64     = 9
    F32     = 10
    VEC2 = 11
    VEC3 = 12
    VEC4 = 13
    MTX44 = 14
    RGBA = 15  # ← the missing one that triggered the crash
    STRING = 16
    HASH = 17
    FILE = 18

    LIST    = 0x80 | 0
    LIST2   = 0x80 | 1
    POINTER = 0x80 | 2
    EMBED   = 0x80 | 3
    LINK    = 0x80 | 4
    OPTION  = 0x80 | 5
    MAP     = 0x80 | 6
    FLAG    = 0x80 | 7


# ---------------------------------------------------------------------------
# 3.  little-endian “cursor” that walks the buffer
# ---------------------------------------------------------------------------
class Cursor:
    def __init__(self, buf: bytes):
        self.buf = buf
        self.pos = 0                        # current offset

    # ---- internal ----------------------------------------------------------
    def _take(self, n: int) -> memoryview:
        if self.pos + n > len(self.buf):
            raise ValueError(f"unexpected EOF @ {self.pos:,}/{len(self.buf):,}")
        mv = memoryview(self.buf)[self.pos : self.pos + n]
        self.pos += n
        return mv

    # ---- primitive readers -------------------------------------------------
    def u8 (self) -> int   : val, = struct.unpack_from("<B", self.buf, self.pos); self.pos += 1; return val
    def u16(self) -> int   : val, = struct.unpack_from("<H", self.buf, self.pos); self.pos += 2; return val
    def u32(self) -> int   : val, = struct.unpack_from("<I", self.buf, self.pos); self.pos += 4; return val
    def u64(self) -> int   : val, = struct.unpack_from("<Q", self.buf, self.pos); self.pos += 8; return val
    def f32(self) -> float : val, = struct.unpack_from("<f", self.buf, self.pos); self.pos += 4; return val

    def pascal_string(self) -> str:
        size = self.u16()
        raw  = self._take(size).tobytes()
        return raw.decode("utf-8", errors="replace")


# ---------------------------------------------------------------------------
# 4.  top-level PROP reader  (header + linked + entries)
# ---------------------------------------------------------------------------
def read_prop(cur: Cursor) -> Dict[str, Any]:
    magic = cur._take(4).tobytes()
    if magic == b"PTCH":
        raise NotImplementedError("PTCH container not implemented yet")
    if magic != b"PROP":
        raise ValueError(f"unexpected magic {magic!r}")

    version = cur.u32()
    out: Dict[str, Any] = {"type": "PROP", "version": version}

    # ---- linked files (version ≥ 2) ---------------------------------------
    if version >= 2:
        linked_cnt = cur.u32()
        out["linked"] = [cur.pascal_string() for _ in range(linked_cnt)]

    # ---- entries map -------------------------------------------------------
    entry_cnt    = cur.u32()
    entry_hashes = [cur.u32() for _ in range(entry_cnt)]

    entries: Dict[int, bytes] = {}
    for root_hash in entry_hashes:
        entry_len = cur.u32()          # total byte length of this embed

        # peek at keyHash + fieldCount so we can rewind later
        key_hash  = cur.u32()
        fld_cnt   = cur.u16()

        # rewind 6 bytes so slice starts with key_hash again
        cur.pos -= 6
        embed_raw = cur._take(entry_len)     # consume exact block

        entries[key_hash] = bytes(embed_raw)

    out["entries"] = entries
    return out


# ---------------------------------------------------------------------------
# 5. convenience wrapper
# ---------------------------------------------------------------------------
def parse_prop(path: Path) -> Dict[str, Any]:
    buf = read_bin_file(path)
    cur = Cursor(buf)
    return read_prop(cur)


# ---------------------------------------------------------------------------
# 6.  CLI
# ---------------------------------------------------------------------------
def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("Usage: python3 ritobin_mac.py <file.bin>")

    bin_path = Path(sys.argv[1]).expanduser().resolve()
    if not bin_path.is_file():
        sys.exit(f"Error: {bin_path} is not a file")

    prop = parse_prop(bin_path)

    print("\n=== PROP header ===")
    print(f"version : {prop['version']}")
    print(f"linked  : {len(prop.get('linked', []))} file(s)")
    print(f"entries : {len(prop['entries']):,} root embed(s)")



# ---------------------------------------------------------------------------
# 7.  Hash look-up
# ---------------------------------------------------------------------------
def _parse_int(tok: str) -> int:
    """Accept `0xDEADBEEF`, `deadbeef`, or plain decimal."""
    if tok.startswith(("0x", "0X")):
        return int(tok, 16)
    try:  # decimal?
        return int(tok, 10)
    except ValueError:  # must be bare hex
        return int(tok, 16)

class HashDB:
    """Loads the four Riot hash tables and resolves 32-bit IDs → names."""
    def __init__(self, root_dir: Path):
        self.idx: dict[int, str] = {}
        for fname in (
            "hashes.binentries.txt",
            "hashes.binfields.txt",
            "hashes.bintypes.txt",
            "hashes.binhashes.txt",
        ):
            path = root_dir / fname
            with path.open("r", encoding="utf-8", errors="ignore") as fh:
                for line in fh:
                    line = line.strip()
                    if not line or line.startswith("#"):
                        continue
                    parts = line.split(maxsplit=1)
                    if len(parts) == 2:
                        # h = int(parts[0], 16 if parts[0].startswith("0x") else 10)
                        h = _parse_int(parts[0])
                        self.idx[h] = parts[1]
        print(f"[info] loaded {len(self.idx):,} hashes")



    def get(self, h: int) -> str:
        return self.idx.get(h, f"0x{h:08x}")


# ---------------------------------------------------------------------------
# 8.  Recursive value reader  (mirrors the C++ visitor)
# ---------------------------------------------------------------------------
def read_value(cur: Cursor, typ: int, hdb: HashDB):
    """Returns a Python object representing one Value node."""
    t = Type(typ)

    # ---- primitives -------------------------------------------------------
    if t in (Type.BOOL,):
        return bool(cur.u8())
    if t in (Type.I8,):
        return struct.unpack("<b", bytes([cur.u8()]))[0]
    if t in (Type.U8,):
        return cur.u8()
    if t in (Type.I16,):
        return struct.unpack("<h", cur._take(2))[0]
    if t in (Type.U16,):
        return cur.u16()
    if t in (Type.I32,):
        return struct.unpack("<i", cur._take(4))[0]
    if t in (Type.U32,):
        return cur.u32()
    if t in (Type.I64,):
        return struct.unpack("<q", cur._take(8))[0]
    if t in (Type.U64,):
        return cur.u64()
    if t == Type.F32:
        return cur.f32()
    if t == Type.STRING:
        return cur.pascal_string()
    if t == Type.HASH:
        return f"hash:{cur.u32():08x}"
    if t == Type.LINK:
        return f"link:{cur.u32():08x}"
    if t == Type.FILE:
        return f"file:{cur.u64():016x}"
    # --- vectors & colour --------------------------------------------------
    if t == Type.VEC2:
        return [cur.f32(), cur.f32()]
    if t == Type.VEC3:
        return [cur.f32(), cur.f32(), cur.f32()]
    if t == Type.VEC4:
        return [cur.f32(), cur.f32(), cur.f32(), cur.f32()]
    if t == Type.MTX44:
        return [cur.f32() for _ in range(16)]
    if t == Type.RGBA:
        # stored as 4 × U8
        return [cur.u8(), cur.u8(), cur.u8(), cur.u8()]
    # ------------------------------------------------------------------
    #  primitives that fit in a single byte
    # ------------------------------------------------------------------
    if t == Type.BOOL:
        return bool(cur.u8())
    if t == Type.FLAG:  # ← NEW
        return bool(cur.u8())  # stores 0 / 1 exactly the same way

    # ---- containers -------------------------------------------------------
    if t in (Type.OPTION,):
        value_type = cur.u8()
        count      = cur.u8()
        return None if count == 0 else read_value(cur, value_type, hdb)
    if t in (Type.LIST, Type.LIST2):
        value_type = cur.u8()
        size       = cur.u32()
        start_pos  = cur.pos
        count      = cur.u32()
        lst = [read_value(cur, value_type, hdb) for _ in range(count)]
        assert cur.pos == start_pos + size, "size mis-match in LIST"
        return lst
    if t == Type.MAP:
        key_type   = cur.u8()
        value_type = cur.u8()
        size       = cur.u32()
        start_pos  = cur.pos
        count      = cur.u32()
        d = {}
        for _ in range(count):
            key   = read_value(cur, key_type, hdb)
            value = read_value(cur, value_type, hdb)
            d[key] = value
        assert cur.pos == start_pos + size, "size mis-match in MAP"
        return d
    if t in (Type.EMBED, Type.POINTER):
        name_hash = cur.u32()
        size      = cur.u32()
        start_pos = cur.pos
        field_cnt = cur.u16()
        obj_name  = hdb.get(name_hash)
        obj = {"__type": obj_name}
        for _ in range(field_cnt):
            field_hash = cur.u32()
            field_type = cur.u8()
            field_name = hdb.get(field_hash)
            obj[field_name] = read_value(cur, field_type, hdb)
        assert cur.pos == start_pos + size, "size mis-match in EMBED"
        return obj
    # ---- unknown ----------------------------------------------------------
    raise NotImplementedError(f"unhandled Type {t}")


# ---------------------------------------------------------------------------
# 9.  Full PROP → Python dict (recursive)
# ---------------------------------------------------------------------------
def parse_full_prop(path: Path, hdb: HashDB):
    buf = read_bin_file(path)
    cur = Cursor(buf)

    # header (reuse code from earlier)
    magic = cur._take(4).tobytes()
    if magic != b"PROP":
        raise ValueError("only PROP v2/v3 supported")
    version = cur.u32()
    prop = {"type": "PROP", "version": version}

    # linked files
    if version >= 2:
        prop["linked"] = [cur.pascal_string() for _ in range(cur.u32())]

    # entries
    entry_cnt    = cur.u32()
    entry_hashes = [cur.u32() for _ in range(entry_cnt)]
    entries = {}
    for root_hash in entry_hashes:
        entry_len = cur.u32()
        slice_start = cur.pos
        # ---- decode embed in-place ---------------------------------------
        key_hash   = cur.u32()
        field_cnt  = cur.u16()
        embed_name = hdb.get(key_hash)
        embed = {"__type": embed_name}
        for _ in range(field_cnt):
            f_hash = cur.u32()
            f_type = cur.u8()
            f_name = hdb.get(f_hash)
            embed[f_name] = read_value(cur, f_type, hdb)
        assert cur.pos == slice_start + entry_len, "embed length error"
        entries[hdb.get(root_hash)] = embed
    prop["entries"] = entries
    return prop


# ---------------------------------------------------------------------------
# 10.  Quick text dumper  (matches the sample style)
# ---------------------------------------------------------------------------
def dump_prop_text(prop: dict, hdb: HashDB, indent="  "):
    def w(line=""):
        out_lines.append(line)

    def fmt_val(v):
        if isinstance(v, str):
            return f"\"{v}\""
        if isinstance(v, bool):
            return "true" if v else "false"
        return str(v)

    def dump_value(v, depth):
        pad = indent * depth
        if isinstance(v, dict):
            tp = v.pop("__type", "embed")
            w(f"{tp} {{")
            for k, val in v.items():
                w(f"{pad}{indent}{k}: ")
                dump_value(val, depth + 1)
            w(f"{pad}}}")
        elif isinstance(v, list):
            w("{")
            for item in v:
                w(f"{pad}{indent}")
                dump_value(item, depth + 1)
            w(f"{pad}}}")
        else:
            w(fmt_val(v))

    out_lines: List[str] = []
    w("#PROP_text\n")
    w("# generated by ritobin_mac.py\n")
    w(f"type: string = \"{prop['type']}\"")
    w(f"version: u32 = {prop['version']}")
    if prop.get("linked"):
        w("linked: list[string] = {")
        for p in prop["linked"]:
            w(f"  \"{p}\"")
        w("}")
    w("entries: map[hash,embed] = {")
    for root, embed in prop["entries"].items():
        w(f"  {root} = ")
        dump_value(embed, 1)
    w("}")
    return "\n".join(out_lines)

def write_python(prop: dict, out_path: Path) -> None:
    """
    Emit a .py file whose sole content is:

        # auto-generated by ritobin_mac.py on 2025-06-28
        data = { ... }      # regular Python dict / list / str / bool / int
    """
    header = (
        f"# auto-generated by ritobin_mac.py on "
        f"{datetime.date.today().isoformat()}\n\n"
        "from __future__ import annotations\n"
        "data = "
    )
    body = pprint.pformat(prop, width=100, compact=False, sort_dicts=False)
    out_path.write_text(header + body + "\n", encoding="utf-8")
    print(f"[done] wrote {out_path.name}")


# ---------------------------------------------------------------------------
# 11.  CLI entry
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: python3 ritobin_mac.py <file.bin>")

    here  = Path(__file__).resolve().parent
    hdb   = HashDB(here / "hashes")
    prop  = parse_full_prop(Path(sys.argv[1]), hdb)

    py_out = Path(sys.argv[1]).with_suffix(".py")
    write_python(prop, py_out)