#!/usr/bin/env python3
"""
FZ Dictionary — Wiktionary Prep Script
========================================
Converts a Kaikki.org Wiktionary JSONL dump into the letter-bucket directory
structure used by the FZ Dictionary Flipper Zero app.

Produces monolingual dictionaries — the source word IS the word being defined,
and the "translation" column holds the formatted definition(s).

DOWNLOAD THE SOURCE FILE
------------------------
Go to https://kaikki.org/dictionary/ and download the full JSONL for your
language. The files you want are named like:

  kaikki.org-dictionary-German.jsonl   (~400 MB, all German Wiktionary entries)
  kaikki.org-dictionary-English.jsonl  (~800 MB, all English Wiktionary entries)

Direct links (check kaikki.org for the latest):
  https://kaikki.org/dictionary/German/kaikki.org-dictionary-German.jsonl
  https://kaikki.org/dictionary/English/kaikki.org-dictionary-English.jsonl

USAGE
-----
  # German monolingual dictionary (auto-names output folder "DE")
  python3 prepare_wiktionary.py split kaikki.org-dictionary-German.jsonl --lang de

  # English monolingual dictionary
  python3 prepare_wiktionary.py split kaikki.org-dictionary-English.jsonl --lang en

  # Custom output folder name
  python3 prepare_wiktionary.py split kaikki.org-dictionary-German.jsonl --lang de --name DUDEN-DE

  # Print stats without writing anything
  python3 prepare_wiktionary.py stats kaikki.org-dictionary-German.jsonl --lang de

  # Filter to specific parts of speech (e.g. only nouns and verbs)
  python3 prepare_wiktionary.py split kaikki.org-dictionary-German.jsonl --lang de --pos noun verb adj

OUTPUT FORMAT (per line in each letter bucket)
----------------------------------------------
  word TAB [pos] sense1 / sense2 / sense3

  Example:
    Hund\\t[Subst.] (mask.) ein Haustier der Familie Canidae; treuer Begleiter des Menschen

The definition is kept to a single line so the app's entry view can display it
with word-wrap. Multiple senses are joined with " / ".

OUTPUT STRUCTURE
----------------
  DE/
    a.txt  b.txt ... z.txt  0.txt  _.txt

Copy the folder to:
  /ext/apps_data/fz_dict_app/DE/

REQUIREMENTS
------------
  Python 3.6+, no external dependencies.
  The input file can be very large (400-800 MB). The script streams it line by
  line so memory usage stays low regardless of file size.
"""

import argparse
import json
import os
import sys
import collections


# ---------------------------------------------------------------------------
# Language configuration
# ---------------------------------------------------------------------------

LANG_CONFIGS = {
    "de": {
        "name": "German",
        "default_folder": "DE",
        "pos_labels": {
            "noun":        "Subst.",
            "verb":        "Verb",
            "adj":         "Adj.",
            "adv":         "Adv.",
            "prep":        "Präp.",
            "conj":        "Konj.",
            "pron":        "Pron.",
            "article":     "Art.",
            "num":         "Num.",
            "intj":        "Interj.",
            "particle":    "Partikel",
            "prefix":      "Präfix",
            "suffix":      "Suffix",
            "name":        "Eigenname",
            "phrase":      "Phrase",
            "proverb":     "Sprichw.",
            "abbrev":      "Abk.",
            "character":   "Zeichen",
            "symbol":      "Symbol",
        },
        # Tags that indicate grammatical gender for nouns
        "gender_tags": {
            "masculine": "mask.",
            "feminine":  "fem.",
            "neuter":    "neutr.",
        },
    },
    "en": {
        "name": "English",
        "default_folder": "EN",
        "pos_labels": {
            "noun":        "n.",
            "verb":        "v.",
            "adj":         "adj.",
            "adv":         "adv.",
            "prep":        "prep.",
            "conj":        "conj.",
            "pron":        "pron.",
            "article":     "art.",
            "num":         "num.",
            "intj":        "interj.",
            "particle":    "part.",
            "prefix":      "prefix",
            "suffix":      "suffix",
            "name":        "proper n.",
            "phrase":      "phrase",
            "proverb":     "prov.",
            "abbrev":      "abbrev.",
            "character":   "char.",
            "symbol":      "symbol",
        },
        "gender_tags": {},
    },
}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def bucket_for(word):
    """Return the letter-bucket filename for a source word."""
    if not word:
        return "_.txt"
    ch = word[0].lower()
    if 'a' <= ch <= 'z':
        return f"{ch}.txt"
    # Handle umlauts and other accented letters — map to their base letter
    umlaut_map = {
        'ä': 'a', 'á': 'a', 'à': 'a', 'â': 'a', 'ã': 'a',
        'ö': 'o', 'ó': 'o', 'ò': 'o', 'ô': 'o', 'õ': 'o',
        'ü': 'u', 'ú': 'u', 'ù': 'u', 'û': 'u',
        'é': 'e', 'è': 'e', 'ê': 'e', 'ë': 'e',
        'í': 'i', 'ì': 'i', 'î': 'i', 'ï': 'i',
        'ß': 's', 'ñ': 'n', 'ç': 'c',
    }
    if ch in umlaut_map:
        return f"{umlaut_map[ch]}.txt"
    if '0' <= ch <= '9':
        return "0.txt"
    return "_.txt"


def clean_gloss(gloss):
    """Strip common Wiktionary annotation patterns from a gloss string."""
    import re
    # Remove parenthetical labels at the start: "(figuratively)", "(dated)", etc.
    gloss = re.sub(r'^\([^)]{1,40}\)\s*', '', gloss)
    # Remove trailing period if it's just punctuation padding
    gloss = gloss.strip()
    return gloss


def format_entry(word, pos, senses, lang_cfg, max_senses=5):
    """
    Build the definition string for one dictionary entry.
    Returns (word, definition) or None if no usable senses found.

    senses: list of sense dicts from the JSONL entry.
    """
    pos_labels  = lang_cfg["pos_labels"]
    gender_tags = lang_cfg.get("gender_tags", {})

    pos_label = pos_labels.get(pos, pos)

    # Collect gender from the first sense that has a gender tag
    gender = ""
    for sense in senses:
        for tag in sense.get("tags", []):
            if tag in gender_tags:
                gender = gender_tags[tag]
                break
        if gender:
            break

    # Build sense strings
    sense_texts = []
    for sense in senses:
        glosses = sense.get("glosses", [])
        if not glosses:
            continue
        # Use the most specific gloss (last in the list)
        gloss = clean_gloss(glosses[-1])
        if not gloss:
            continue
        # Skip meta-glosses that are just labels
        if gloss.startswith("(") and gloss.endswith(")"):
            continue
        sense_texts.append(gloss)
        if len(sense_texts) >= max_senses:
            break

    if not sense_texts:
        return None

    # Build header: "[pos] (gender)"
    header_parts = [f"[{pos_label}]"]
    if gender:
        header_parts.append(f"({gender})")
    header = " ".join(header_parts)

    # Join senses
    if len(sense_texts) == 1:
        definition = f"{header} {sense_texts[0]}"
    else:
        numbered = "  ".join(f"{i+1}. {s}" for i, s in enumerate(sense_texts))
        definition = f"{header} {numbered}"

    # Clamp to a safe display length (app ENTRY_TEXT_LEN = 512)
    if len(definition) > 500:
        definition = definition[:497] + "..."

    return word, definition


def parse_jsonl(path, lang_name, allowed_pos=None):
    """
    Stream the JSONL file and yield (word, pos, senses) for matching entries.
    lang_name: e.g. "German" or "English" — must match the "lang" field.
    allowed_pos: set of POS strings to include, or None for all.
    """
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue

            # Filter by language
            if obj.get("lang") != lang_name:
                continue

            word = obj.get("word", "").strip()
            if not word:
                continue

            pos = obj.get("pos", "").strip()
            if allowed_pos and pos not in allowed_pos:
                continue

            senses = obj.get("senses", [])
            if not senses:
                continue

            yield word, pos, senses


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def cmd_stats(args):
    lang_cfg  = LANG_CONFIGS[args.lang]
    lang_name = lang_cfg["name"]
    path      = args.input

    if not os.path.isfile(path):
        sys.exit(f"Error: file not found: {path}")

    file_size = os.path.getsize(path)
    print(f"File      : {path}")
    print(f"Size      : {file_size / 1024 / 1024:.1f} MB")
    print(f"Language  : {lang_name}")
    print(f"\nScanning... (this may take a minute for large files)")

    bucket_counts = collections.Counter()
    pos_counts    = collections.Counter()
    total = 0
    skipped = 0

    for word, pos, senses in parse_jsonl(path, lang_name, None):
        result = format_entry(word, pos, senses, lang_cfg)
        if result is None:
            skipped += 1
            continue
        total += 1
        pos_counts[pos] += 1
        bucket_counts[bucket_for(word)] += 1

    print(f"\nTotal entries   : {total:,}")
    print(f"Skipped (no def): {skipped:,}")

    print(f"\nTop parts of speech:")
    for pos, count in pos_counts.most_common(10):
        print(f"  {pos:12s}  {count:>7,}")

    print(f"\nEntries per bucket:")
    max_count = max(bucket_counts.values()) if bucket_counts else 1
    for bkt in sorted(bucket_counts):
        bar = "#" * min(40, bucket_counts[bkt] * 40 // max_count)
        print(f"  {bkt:6s}  {bucket_counts[bkt]:>7,}  {bar}")


def cmd_split(args):
    lang_cfg  = LANG_CONFIGS[args.lang]
    lang_name = lang_cfg["name"]
    path      = args.input

    if not os.path.isfile(path):
        sys.exit(f"Error: file not found: {path}")

    allowed_pos = set(args.pos) if args.pos else None

    src_dir  = os.path.dirname(path) or "."
    out_name = args.name or lang_cfg["default_folder"]
    out_dir  = os.path.join(src_dir, out_name)

    if os.path.exists(out_dir) and not os.path.isdir(out_dir):
        sys.exit(f"Error: output path exists and is not a directory: {out_dir}")

    os.makedirs(out_dir, exist_ok=True)

    print(f"Language  : {lang_name}")
    print(f"Output    : {out_dir}")
    if allowed_pos:
        print(f"POS filter: {', '.join(sorted(allowed_pos))}")
    print()

    handles       = {}
    bucket_counts = collections.Counter()
    total   = 0
    skipped = 0
    dot_every = 10_000

    def get_handle(bkt):
        if bkt not in handles:
            handles[bkt] = open(
                os.path.join(out_dir, bkt), "w", encoding="utf-8", newline="\n"
            )
        return handles[bkt]

    print("Splitting", end="", flush=True)

    for word, pos, senses in parse_jsonl(path, lang_name, allowed_pos):
        result = format_entry(word, pos, senses, lang_cfg, max_senses=args.max_senses)
        if result is None:
            skipped += 1
            continue

        w, definition = result
        total += 1
        if total % dot_every == 0:
            print(".", end="", flush=True)

        bkt = bucket_for(w)
        fh  = get_handle(bkt)
        fh.write(f"{w}\t{definition}\n")
        bucket_counts[bkt] += 1

    for fh in handles.values():
        fh.close()

    print(f"\n\nDone -- {total:,} entries written, {skipped:,} skipped (no usable definition).\n")

    print("Bucket summary:")
    total_size = 0
    for bkt in sorted(bucket_counts):
        fpath = os.path.join(out_dir, bkt)
        fsize = os.path.getsize(fpath)
        total_size += fsize
        print(f"  {bkt:6s}  {bucket_counts[bkt]:>7,} entries  ({fsize / 1024:.0f} KB)")

    print(f"\nTotal size : {total_size / 1024 / 1024:.1f} MB")
    print(f"\nCopy to Flipper Zero SD card:")
    print(f"  /ext/apps_data/fz_dict_app/{out_name}/")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser():
    p = argparse.ArgumentParser(
        description="Convert a Kaikki.org Wiktionary JSONL dump for the FZ Dictionary app.",
    )
    sub = p.add_subparsers(dest="command", required=True)

    # ── split ─────────────────────────────────────────────────────────────
    sp = sub.add_parser(
        "split",
        help="Convert and split JSONL into letter-bucket directories.",
    )
    sp.add_argument("input", help="Input Kaikki JSONL file")
    sp.add_argument(
        "--lang", "-l", required=True, choices=list(LANG_CONFIGS),
        metavar="LANG",
        help=f"Language code: {', '.join(LANG_CONFIGS)} "
             f"(determines definition labels and folder name)",
    )
    sp.add_argument(
        "--name", "-n", metavar="DIRNAME", default=None,
        help="Output folder name (default: DE or EN based on --lang)",
    )
    sp.add_argument(
        "--pos", metavar="POS", nargs="+", default=None,
        help="Only include these parts of speech, e.g. --pos noun verb adj. "
             "Default: include all.",
    )
    sp.add_argument(
        "--max-senses", type=int, default=5, metavar="N",
        help="Maximum number of senses to include per entry (default: 5)",
    )

    # ── stats ─────────────────────────────────────────────────────────────
    st = sub.add_parser(
        "stats",
        help="Show statistics without writing any output.",
    )
    st.add_argument("input", help="Input Kaikki JSONL file")
    st.add_argument(
        "--lang", "-l", required=True, choices=list(LANG_CONFIGS),
        metavar="LANG",
        help="Language code",
    )

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
