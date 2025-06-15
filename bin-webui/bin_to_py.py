import os
import argparse
from pathlib import Path

def read_bin_file(filepath: str) -> bytes:
    """Reads a binary file and returns the raw byte content."""
    with open(filepath, "rb") as f:
        return f.read()

def write_py_file(bin_data: bytes, output_path: str, variable_name: str = "bin_data") -> None:
    """Writes the bin data to a Python file as a bytes object."""
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("# Auto-generated Python file from .bin\n")
        f.write("# Original binary content is included as a bytes object\n\n")
        f.write(f"{variable_name} = bytes([\n")

        # Format: 16 bytes per line
        for i in range(0, len(bin_data), 16):
            chunk = bin_data[i:i+16]
            line = ", ".join(f"0x{b:02x}" for b in chunk)
            f.write(f"    {line},\n")

        f.write("])\n")

def main():
    parser = argparse.ArgumentParser(description="Convert a Riot .bin file to a Python-readable format")
    parser.add_argument("input", help="Path to the .bin file")
    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.exists() or input_path.suffix != ".bin":
        print(f"[ERROR] Input file must exist and have a .bin extension: {input_path}")
        return

    output_path = input_path.with_suffix(".py")

    bin_data = read_bin_file(str(input_path))
    write_py_file(bin_data, str(output_path))

    print(f"[OK] Converted '{input_path.name}' → '{output_path.name}'")

if __name__ == "__main__":
    main()
