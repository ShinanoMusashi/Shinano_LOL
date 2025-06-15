import struct
import os
import xxhash

# === CONFIG ===
HASH_TABLE_PATHS = [
    "./hashtable/hashes/lol/hashes.game.txt.0",
    "./hashtable/hashes/lol/hashes.game.txt.1"
]
OUTPUT_DIR = "output_chunks"
CHUNK_TABLE_OFFSET = 0x120
ENTRY_SIZE = 16
MAX_PATH_LENGTH = 240  # safe for macOS
MAX_CHUNKS = 10000  # avoid garbage at the end

# === LOAD HASH TABLE ===
def load_hash_table(paths):
    hash_map = {}
    for path in paths:
        if not os.path.exists(path):
            continue
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                parts = line.strip().split(" ", 1)
                if len(parts) == 2:
                    hash_val, path_str = parts
                    hash_map[int(hash_val, 16)] = path_str
    return hash_map

# === FILE TYPE GUESSING ===
def guess_extension(data):
    if data.startswith(b"r3d2Mesh"):
        return "scb"
    elif data.startswith(b"r3d2sklt") or b"SKL " in data[:20]:
        return "skl"
    elif data.startswith(b"DDS "):
        return "dds"
    elif data.startswith(b"PROP"):
        return "bin"
    elif data.startswith(b"PreLoad"):
        return "preload"
    elif data.startswith(b"[Obj"):
        return "sco"
    elif data.startswith(b"TEX\0"):
        return "tex"
    elif data.startswith(b"\x89PNG"):
        return "png"
    return "bin"

# === MAIN EXTRACTION ===
def extract_chunks(wad_path):
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    hash_table = load_hash_table(HASH_TABLE_PATHS)

    WAD_FILE_PATH = wad_path
    wad_size = os.path.getsize(wad_path)
    
    with open(WAD_FILE_PATH, "rb") as f:
        f.seek(CHUNK_TABLE_OFFSET)

        for index in range(MAX_CHUNKS):
            entry = f.read(ENTRY_SIZE)
            if len(entry) < ENTRY_SIZE:
                break  # end of valid table

            path_hash, offset, size = struct.unpack("<QII", entry)

            # Skip suspicious chunks
            if offset == 0 or size == 0 or offset + size > wad_size:
                print(f"⚠️  Skipping invalid chunk #{index}: offset={offset}, size={size}")
                continue

            cur_pos = f.tell()
            f.seek(offset)
            data = f.read(size)
            f.seek(cur_pos)

            ext = guess_extension(data)
            resolved_name = hash_table.get(path_hash, f"{path_hash:016x}.{ext}")
            output_path = os.path.join(OUTPUT_DIR, resolved_name)

            # Path length safety fallback
            if len(output_path.encode("utf-8")) > MAX_PATH_LENGTH:
                print(f"⚠️  Truncating long path for hash: {path_hash:016x}")
                resolved_name = f"{path_hash:016x}.{ext}"
                output_path = os.path.join(OUTPUT_DIR, resolved_name)

            try:
                os.makedirs(os.path.dirname(output_path), exist_ok=True)
                with open(output_path, "wb") as out:
                    out.write(data)
                print(f"[{index}] Saved: {resolved_name} ({size} bytes)")
            except OSError as e:
                print(f"❌ Failed to save chunk {index}: {e}")
