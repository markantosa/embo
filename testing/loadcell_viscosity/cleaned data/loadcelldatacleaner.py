#!/usr/bin/env python3
"""
Filter serial-capture CSV files to keep only rows whose serial_data
column contains a "Motion >" log entry.

Usage:
    python filter_motion.py input.csv [output.csv]
    python filter_motion.py *.csv --in-place

If no output path is given, writes "<input>_filtered.csv" next to the input.
"""

import argparse
import csv
import sys
from pathlib import Path


def filter_motion_rows(input_path: Path, output_path: Path, keyword: str = "Motion >") -> tuple[int, int]:
    """Keep only rows whose serial_data field contains `keyword`.

    Returns (rows_kept, rows_total).
    """
    with input_path.open("r", newline="", encoding="utf-8", errors="replace") as f_in:
        reader = csv.DictReader(f_in)
        fieldnames = reader.fieldnames
        if fieldnames is None or "serial_data" not in fieldnames:
            raise ValueError(f"{input_path}: no 'serial_data' column found (columns: {fieldnames})")

        rows_total = 0
        kept_rows = []
        for row in reader:
            rows_total += 1
            if keyword in (row.get("serial_data") or ""):
                kept_rows.append(row)

    with output_path.open("w", newline="", encoding="utf-8") as f_out:
        writer = csv.DictWriter(f_out, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(kept_rows)

    return len(kept_rows), rows_total


def main():
    parser = argparse.ArgumentParser(description="Keep only 'Motion >' rows from serial-capture CSVs.")
    parser.add_argument("inputs", nargs="+", type=Path, help="Input CSV file(s)")
    parser.add_argument("-o", "--output", type=Path, help="Output path (only valid with a single input file)")
    parser.add_argument("--in-place", action="store_true", help="Overwrite each input file with the filtered result")
    parser.add_argument("--keyword", default="Motion >", help="Substring to filter on (default: 'Motion >')")
    args = parser.parse_args()

    if args.output and len(args.inputs) > 1:
        parser.error("--output can only be used with a single input file")

    for input_path in args.inputs:
        if not input_path.exists():
            print(f"Skipping {input_path}: file not found", file=sys.stderr)
            continue

        if args.in_place:
            output_path = input_path
            tmp_path = input_path.with_suffix(input_path.suffix + ".tmp")
            kept, total = filter_motion_rows(input_path, tmp_path, args.keyword)
            tmp_path.replace(output_path)
        else:
            output_path = args.output if args.output else input_path.with_name(f"{input_path.stem}_filtered{input_path.suffix}")
            kept, total = filter_motion_rows(input_path, output_path, args.keyword)

        print(f"{input_path.name}: kept {kept}/{total} rows -> {output_path}")


if __name__ == "__main__":
    main()