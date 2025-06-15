#!/usr/bin/env python3
"""
League of Legends *.wad.client* extractor
─────────────────────────────────────────
✓ Decompresses every chunk (Zstandard)
✓ Uses Riot + CommunityDragon hash tables
✓ Merges patch layers (last duplicate wins)
✓ Reads the internal manifest (chunk 0x0000000300000180)
✓ Writes assets to real paths, truncating any path-component that
  would exceed 120 bytes (macOS/Linux limit ≈255) while keeping a
  hash tag for uniqueness
"""

from __future__ import annotations
import os, struct, pathlib, zstandard as zstd, xxhash, json

# ── configuration ──────────────────────────────────────────────────────────
HASH_TABLE_PATHS = [
    "./hashtable/hashes/lol/hashes.game.txt.0",
    "./hashtable/hashes/lol/hashes.game.txt.1",
]
OUTPUT_DIR     = "output_chunks"
TABLE_OFFSET   = 0x120          # chunk table begins here
ENTRY_SIZE     = 16
MAX_CHUNKS     = 50_000
ZSTD_MAGIC     = b"\x28\xB5\x2F\xFD"
MANIFEST_HASH  = 0x0000000300000180  # chunk that stores pathHashes[]

# ── helpers ───────────────────────────────────────────────────────────────
def _maybe_decompress(data: bytes) -> bytes:
    return zstd.ZstdDecompressor().decompress(data) if data.startswith(ZSTD_MAGIC) else data

def _guess_ext(data: bytes) -> str:
    if data.startswith(b"DDS "):      return "dds"
    if data.startswith(b"\x89PNG"):   return "png"
    if data.startswith(b"PROP"):      return "bin"
    if data[:8] in (b"SKN\x00", b"SKL\x00"):
        return "skn" if data[:4] == b"SKN\x00" else "skl"
    if data.startswith(b"r3d2Mesh"):  return "scb"
    if data.startswith(b"[Obj"):      return "sco"
    return "bin"

def _load_hash_table(paths: list[str]) -> dict[int, str]:
    mapping: dict[int, str] = {}
    for p in paths:
        if not os.path.exists(p):
            continue
        with open(p, encoding="utf-8") as fp:
            for line in fp:
                h, name = line.strip().split(" ", 1)
                mapping[int(h, 16)] = name
    return mapping

# ── manifest decoder (exact layout) ───────────────────────────────────────
def _parse_manifest(chunk: bytes) -> dict[int, str]:
    MAGIC = b"pathHashes\x00"
    pos   = chunk.find(MAGIC)
    if pos == -1:
        return {}
    pos  += len(MAGIC)
    pos   = (pos + 3) & ~3           # 4-byte align
    pos  += 4                        # skip flags/padding
    count, = struct.unpack_from("<I", chunk, pos)
    pos  += 4

    out: dict[int, str] = {}
    for _ in range(count):
        h, strlen = struct.unpack_from("<QI", chunk, pos)
        pos += 12
        s   = chunk[pos : pos + strlen].decode("utf-8", "replace")
        pos += strlen
        out[h] = s
    return out

# truncate any *single* path component that would blow past 120 bytes
def _safe_component(comp: str, max_len: int = 120) -> str:
    if len(comp.encode()) <= max_len:
        return comp
    # keep extension, keep first half, add hash tag
    base, ext = os.path.splitext(comp)
    tag       = xxhash.xxh32(comp).hexdigest()
    return f"{base[:max_len//2]}_{tag}{ext}"

# ── main extractor ────────────────────────────────────────────────────────
def extract_wad(wad_path: pathlib.Path, out_root: pathlib.Path) -> None:
    wad_bytes = wad_path.read_bytes()
    wad_size  = len(wad_bytes)

    # 1) hash → payload (later duplicate overrides earlier)
    chunks: dict[int, bytes] = {}
    for n in range(MAX_CHUNKS):
        off = TABLE_OFFSET + n * ENTRY_SIZE
        if off + ENTRY_SIZE > wad_size:
            break
        h, loc, size = struct.unpack_from("<QII", wad_bytes, off)
        if not any((h, loc, size)):
            break                    # zero entry marks end of table
        if size == 0 or loc + size > wad_size:
            continue                 # corrupt/span-out-of-file
        chunks[h] = _maybe_decompress(wad_bytes[loc:loc+size])

    # 2) build hash → name map (static list + per-file manifest)
    names = _load_hash_table(HASH_TABLE_PATHS)
    if MANIFEST_HASH in chunks:
        names.update(_parse_manifest(chunks[MANIFEST_HASH]))

    # 3) write assets
    out_dir = out_root / wad_path.stem
    for h, data in chunks.items():
        ext      = _guess_ext(data)
        rel_path = names.get(h, f"{h:016x}.{ext}")
        rel_path = "/".join(_safe_component(p) for p in rel_path.split("/"))

        dest = out_dir / rel_path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        print(f"[✓] {dest.relative_to(out_root)}  {len(data):,} B")

# ── simple CLI ────────────────────────────────────────────────────────────
if __name__ == "__main__":
    import sys
    if len(sys.argv) != 2:
        raise SystemExit("usage: extract.py <file.wad.client>")
    extract_wad(pathlib.Path(sys.argv[1]), pathlib.Path(OUTPUT_DIR))
