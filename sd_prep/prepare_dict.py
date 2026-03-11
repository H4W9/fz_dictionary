#!/usr/bin/env python3
"""
FZ Dictionary SD Card Prep Script
===================================
Converts a full dict.cc export file into the letter-bucket directory
structure required by the FZ Dictionary Flipper Zero app.

USAGE
-----
  # Most common: split a full dict.cc export (creates EN-DE/ folder alongside the .txt)
  python3 prepare_dict.py split EN-DE.txt

  # Specify a custom output directory name
  python3 prepare_dict.py split EN-DE.txt --name MY-DICT

  # Specify exact output path
  python3 prepare_dict.py split EN-DE.txt --out /path/to/EN-DE

  # Print statistics without writing anything
  python3 prepare_dict.py stats EN-DE.txt

OUTPUT STRUCTURE
----------------
  EN-DE/
    a.txt   <- all entries whose source word starts with A or a
    b.txt
    ...
    z.txt
    0.txt   <- entries starting with a digit (0-9)
    _.txt   <- entries starting with any other character

Copy the whole folder to:
  /ext/apps_data/fz_dict_app/EN-DE/

DICT.CC FILE FORMAT
-------------------
  Lines starting with '#' are header/comment lines and are skipped.
  Each entry line:   source_word TAB translation [TAB optional_notes]
"""

import argparse
import os
import sys
import collections


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def bucket_for(word):
    """Return the letter-bucket filename (a.txt, 0.txt, _.txt) for a source word."""
    if not word:
        return "_.txt"
    ch = word[0].lower()
    if 'a' <= ch <= 'z':
        return f"{ch}.txt"
    if '0' <= ch <= '9':
        return "0.txt"
    return "_.txt"


def parse_dict_file(path):
    """
    Yield (source, translation, notes) tuples for every valid entry.
    Skips blank lines and lines starting with '#'.
    """
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            source = parts[0].strip()
            translation = parts[1].strip()
            notes = "\t".join(parts[2:]).strip() if len(parts) > 2 else ""
            if not source or not translation:
                continue
            yield source, translation, notes


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def cmd_stats(args):
    """Print statistics about the source file."""
    path = args.input
    if not os.path.isfile(path):
        sys.exit(f"Error: file not found: {path}")

    file_size = os.path.getsize(path)
    print(f"File  : {path}")
    print(f"Size  : {file_size / 1024 / 1024:.1f} MB  ({file_size:,} bytes)")

    counts = collections.Counter()
    total = 0
    max_src = 0
    max_trans = 0

    for source, translation, notes in parse_dict_file(path):
        total += 1
        bkt = bucket_for(source)
        counts[bkt] += 1
        if len(source) > max_src:
            max_src = len(source)
        if len(translation) > max_trans:
            max_trans = len(translation)

    print(f"\nTotal entries : {total:,}")
    print(f"Max src len   : {max_src} chars")
    print(f"Max trans len : {max_trans} chars")
    print(f"\nEntries per bucket:")
    max_count = max(counts.values()) if counts else 1
    for bkt in sorted(counts):
        bar = "#" * min(40, counts[bkt] * 40 // max_count)
        print(f"  {bkt:6s}  {counts[bkt]:>7,}  {bar}")

    print(f"\nEstimated split sizes (rough):")
    for bkt in sorted(counts):
        est_kb = counts[bkt] * 30 / 1024
        print(f"  {bkt:6s}  ~{est_kb:>7.0f} KB")


def cmd_split(args):
    """Split the source file into the letter-bucket directory structure."""
    src_path = args.input
    if not os.path.isfile(src_path):
        sys.exit(f"Error: file not found: {src_path}")

    # Determine output directory
    if args.out:
        out_dir = args.out
    elif args.name:
        out_dir = os.path.join(os.path.dirname(src_path) or ".", args.name)
    else:
        stem = os.path.splitext(os.path.basename(src_path))[0]
        out_dir = os.path.join(os.path.dirname(src_path) or ".", stem)

    if os.path.exists(out_dir) and not os.path.isdir(out_dir):
        sys.exit(f"Error: output path exists and is not a directory: {out_dir}")

    os.makedirs(out_dir, exist_ok=True)
    print(f"Output directory : {out_dir}")

    handles = {}
    bucket_counts = collections.Counter()
    total = 0

    def get_handle(bkt):
        if bkt not in handles:
            p = os.path.join(out_dir, bkt)
            handles[bkt] = open(p, "w", encoding="utf-8", newline="\n")
        return handles[bkt]

    print("Splitting", end="", flush=True)
    dot_every = 50_000

    for source, translation, notes in parse_dict_file(src_path):
        total += 1
        if total % dot_every == 0:
            print(".", end="", flush=True)

        bkt = bucket_for(source)
        fh = get_handle(bkt)
        if notes:
            fh.write(f"{source}\t{translation}\t{notes}\n")
        else:
            fh.write(f"{source}\t{translation}\n")
        bucket_counts[bkt] += 1

    for fh in handles.values():
        fh.close()

    print(f"\n\nDone -- {total:,} entries written to {len(handles)} files.\n")

    print("Bucket summary:")
    total_size = 0
    for bkt in sorted(bucket_counts):
        fpath = os.path.join(out_dir, bkt)
        fsize = os.path.getsize(fpath)
        total_size += fsize
        print(f"  {bkt:6s}  {bucket_counts[bkt]:>7,} entries  ({fsize / 1024:.0f} KB)")

    print(f"\nTotal split size : {total_size / 1024 / 1024:.1f} MB")
    print(f"\nCopy to Flipper Zero SD card:")
    print(f"  /ext/apps_data/fz_dict_app/{os.path.basename(out_dir)}/")
    print(f"\nSearch tip: because each search only reads one letter file,")
    print(f"  the largest single-letter bucket is the worst case.")
    for bkt in sorted(bucket_counts, key=lambda b: -bucket_counts[b])[:3]:
        fsize = os.path.getsize(os.path.join(out_dir, bkt))
        print(f"  Largest: {bkt}  ({fsize/1024:.0f} KB  /  {bucket_counts[bkt]:,} entries)")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser():
    p = argparse.ArgumentParser(
        description="Prepare a dict.cc export for the FZ Dictionary Flipper Zero app.",
    )
    sub = p.add_subparsers(dest="command", required=True)

    sp = sub.add_parser(
        "split",
        help="Split a flat dict.cc .txt file into letter-bucket directories.",
    )
    sp.add_argument("input", help="Input dict.cc .txt file")
    sp.add_argument(
        "--name", "-n", metavar="DIRNAME", default=None,
        help="Name for the output folder (default: stem of input filename)",
    )
    sp.add_argument(
        "--out", "-o", metavar="PATH", default=None,
        help="Full output directory path (overrides --name)",
    )

    st = sub.add_parser(
        "stats",
        help="Print statistics about the dict.cc file without writing any output.",
    )
    st.add_argument("input", help="Input dict.cc .txt file")

    return p


def main():
    parser = build_parser()
    args = parser.parse_args()
    if args.command == "split":
        cmd_split(args)
    elif args.command == "stats":
        cmd_stats(args)
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
