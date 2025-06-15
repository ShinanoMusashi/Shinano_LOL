import re
import sys
from pathlib import Path
import importlib.util

class RiotHashUnhasher:
    def __init__(self, hash_dir: str):
        self.hash_map = {}
        self._load_all(hash_dir)

    def _load_all(self, hash_dir: str):
        hash_dir_path = Path(hash_dir)
        if not hash_dir_path.exists():
            raise FileNotFoundError(f"Hash directory not found: {hash_dir_path}")

        for file in hash_dir_path.glob("hashes.*.txt*"):
            with open(file, encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith("#"):
                        continue
                    match = re.match(r"^(0x[0-9a-fA-F]+)\s+(.+)$", line)
                    if match:
                        hash_hex, unhashed_str = match.groups()
                        hash_val = int(hash_hex, 16)
                        self.hash_map[hash_val] = unhashed_str

    def unhash(self, value: int) -> str:
        return self.hash_map.get(value, f"0x{value:016X}")

def import_bin_data(py_file_path: str):
    spec = importlib.util.spec_from_file_location("bin_module", py_file_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return getattr(module, "bin_data", None)

def extract_unhashed_keys(bin_data: bytes, unhasher: RiotHashUnhasher, max_count: int = 2000):
    results = []
    for i in range(0, min(len(bin_data), max_count * 8), 8):
        chunk = bin_data[i:i+8]
        if len(chunk) < 8:
            continue
        val = int.from_bytes(chunk, "little")
        unhashed = unhasher.unhash(val)
        results.append((f"0x{val:016X}", unhashed))
    return results

def write_output(output_path: str, unhashed_pairs: list, source: str):
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(f"# Auto-unhashed from: {source}\n\n")
        f.write("unhashed_fields = [\n")
        for hexval, string in unhashed_pairs:
            f.write(f"    ({hexval!r}, {string!r}),\n")
        f.write("]\n")

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 hash_unhasher.py <file.py>")
        sys.exit(1)

    input_py = Path(sys.argv[1])
    if not input_py.exists():
        print(f"[!] File not found: {input_py}")
        sys.exit(1)

    unhasher = RiotHashUnhasher("tools/hashtable/hashes/lol/")
    bin_data = import_bin_data(str(input_py))

    if bin_data is None:
        print("[!] 'bin_data' not found in the input file.")
        sys.exit(1)

    results = extract_unhashed_keys(bin_data, unhasher)
    output_py = input_py.with_name(input_py.stem + "_unhashed.py")
    write_output(output_py, results, input_py.name)

    print(f"[✓] Unhashed output saved to: {output_py}")

if __name__ == "__main__":
    main()
