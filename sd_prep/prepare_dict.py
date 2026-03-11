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

  # Split into BOTH directions from one file (creates EN-DE/ and DE-EN/)
  python3 prepare_dict.py split EN-DE.txt --bidirectional

  # Give the two folders custom names (forward first, reversed second)
  python3 prepare_dict.py split EN-DE.txt --bidirectional --name EN-DE --reverse-name DE-EN

  # One direction with a custom folder name
  python3 prepare_dict.py split EN-DE.txt --name MY-DICT

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

  DE-EN/    <- created by --bidirectional (columns swapped)
    a.txt
    ...

Copy both folders to:
  /ext/apps_data/fz_dict_app/

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


def infer_reverse_name(forward_name):
    """
    Try to flip a language-pair name automatically.
    'EN-DE' -> 'DE-EN', 'en_de' -> 'de_en', etc.
    Falls back to appending '-REV' if no separator is found.
    """
    for sep in ('-', '_', '.'):
        parts = forward_name.split(sep)
        if len(parts) == 2:
            return sep.join(reversed(parts))
    return forward_name + "-REV"


# ---------------------------------------------------------------------------
# Core split logic (used by both directions)
# ---------------------------------------------------------------------------

def split_into_dir(entries, out_dir, label="Splitting"):
    """
    Write (source, translation, notes) entries into letter-bucket files
    inside out_dir. Returns (total_count, bucket_counts).
    entries must be an iterable — it is consumed once.
    """
    os.makedirs(out_dir, exist_ok=True)

    handles = {}
    bucket_counts = collections.Counter()
    total = 0
    dot_every = 50_000

    def get_handle(bkt):
        if bkt not in handles:
            handles[bkt] = open(
                os.path.join(out_dir, bkt), "w", encoding="utf-8", newline="\n"
            )
        return handles[bkt]

    print(f"{label}", end="", flush=True)

    for source, translation, notes in entries:
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

    print()
    return total, bucket_counts


def print_bucket_summary(out_dir, bucket_counts):
    total_size = 0
    for bkt in sorted(bucket_counts):
        fpath = os.path.join(out_dir, bkt)
        fsize = os.path.getsize(fpath)
        total_size += fsize
        print(f"  {bkt:6s}  {bucket_counts[bkt]:>7,} entries  ({fsize / 1024:.0f} KB)")
    print(f"  Total : {total_size / 1024 / 1024:.1f} MB")

    # Largest bucket tip
    top = sorted(bucket_counts, key=lambda b: -bucket_counts[b])[:1]
    for bkt in top:
        fsize = os.path.getsize(os.path.join(out_dir, bkt))
        print(f"  Largest bucket: {bkt}  ({fsize/1024:.0f} KB  /  {bucket_counts[bkt]:,} entries)")


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

    fwd_counts = collections.Counter()
    rev_counts = collections.Counter()
    total = 0
    max_src = 0
    max_trans = 0

    for source, translation, notes in parse_dict_file(path):
        total += 1
        fwd_counts[bucket_for(source)] += 1
        rev_counts[bucket_for(translation)] += 1
        if len(source) > max_src:
            max_src = len(source)
        if len(translation) > max_trans:
            max_trans = len(translation)

    print(f"\nTotal entries : {total:,}")
    print(f"Max src len   : {max_src} chars")
    print(f"Max trans len : {max_trans} chars")

    print(f"\nForward direction — entries per bucket:")
    max_count = max(fwd_counts.values()) if fwd_counts else 1
    for bkt in sorted(fwd_counts):
        bar = "#" * min(40, fwd_counts[bkt] * 40 // max_count)
        print(f"  {bkt:6s}  {fwd_counts[bkt]:>7,}  {bar}")

    print(f"\nReverse direction (--bidirectional) — entries per bucket:")
    max_count = max(rev_counts.values()) if rev_counts else 1
    for bkt in sorted(rev_counts):
        bar = "#" * min(40, rev_counts[bkt] * 40 // max_count)
        print(f"  {bkt:6s}  {rev_counts[bkt]:>7,}  {bar}")


def cmd_split(args):
    """Split the source file into one or two letter-bucket directory structures."""
    src_path = args.input
    if not os.path.isfile(src_path):
        sys.exit(f"Error: file not found: {src_path}")

    src_dir = os.path.dirname(src_path) or "."
    stem    = os.path.splitext(os.path.basename(src_path))[0]

    # Forward directory
    fwd_dir = args.out or os.path.join(src_dir, args.name or stem)
    if os.path.exists(fwd_dir) and not os.path.isdir(fwd_dir):
        sys.exit(f"Error: output path exists and is not a directory: {fwd_dir}")

    # Reverse directory (only when --bidirectional)
    rev_dir = None
    if args.bidirectional:
        rev_name = args.reverse_name or infer_reverse_name(args.name or stem)
        rev_dir = os.path.join(src_dir, rev_name)
        if os.path.exists(rev_dir) and not os.path.isdir(rev_dir):
            sys.exit(f"Error: reverse output path exists and is not a directory: {rev_dir}")

    # ── Forward pass ──────────────────────────────────────────────────────
    print(f"\nForward  -> {fwd_dir}")
    fwd_total, fwd_counts = split_into_dir(
        parse_dict_file(src_path), fwd_dir, label="  Splitting forward "
    )
    print(f"  {fwd_total:,} entries written.\n")
    print("  Bucket summary:")
    print_bucket_summary(fwd_dir, fwd_counts)

    # ── Reverse pass (columns swapped) ────────────────────────────────────
    if rev_dir is not None:
        print(f"\nReverse  -> {rev_dir}")

        def reversed_entries():
            for source, translation, notes in parse_dict_file(src_path):
                yield translation, source, notes

        rev_total, rev_counts = split_into_dir(
            reversed_entries(), rev_dir, label="  Splitting reverse "
        )
        print(f"  {rev_total:,} entries written.\n")
        print("  Bucket summary:")
        print_bucket_summary(rev_dir, rev_counts)

    # ── Final instructions ────────────────────────────────────────────────
    print(f"\nCopy to Flipper Zero SD card:")
    print(f"  /ext/apps_data/fz_dict_app/{os.path.basename(fwd_dir)}/")
    if rev_dir:
        print(f"  /ext/apps_data/fz_dict_app/{os.path.basename(rev_dir)}/")
    print(f"\nBoth dictionaries will appear in the app — switch with L/R on the Search row.")


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
        help="Name for the forward output folder (default: stem of input filename)",
    )
    sp.add_argument(
        "--out", "-o", metavar="PATH", default=None,
        help="Full path for the forward output directory (overrides --name)",
    )
    sp.add_argument(
        "--bidirectional", "-b", action="store_true",
        help="Also produce a reversed dictionary (columns swapped) in a second folder",
    )
    sp.add_argument(
        "--reverse-name", "-r", metavar="DIRNAME", default=None,
        help="Name for the reversed output folder (default: auto-flipped, e.g. EN-DE -> DE-EN)",
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
