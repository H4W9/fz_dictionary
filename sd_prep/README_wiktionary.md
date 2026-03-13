# prepare_wiktionary.py

Converts a [Kaikki.org](https://kaikki.org/dictionary/) Wiktionary JSONL dump
into the letter-bucket directory structure used by the FZ Dictionary Flipper
Zero app.

Produces **monolingual** dictionaries — the source word is the word being
defined, and the definition column contains the formatted sense(s).

---

## Requirements

- Python 3.6 or newer
- No external dependencies
- The input JSONL file (downloaded separately — see below)

---

## Getting the Source File

Go to **https://kaikki.org/dictionary/** and download the full JSONL dump for
your language. Choose the **"All entries"** file at the bottom of each language
page, not the individual by-POS files.

| Language | File | Approx. size |
|----------|------|-------------|
| German   | `kaikki.org-dictionary-German.jsonl`  | ~400 MB |
| English  | `kaikki.org-dictionary-English.jsonl` | ~800 MB |

Direct links (check kaikki.org if these change):
- https://kaikki.org/dictionary/German/kaikki.org-dictionary-German.jsonl
- https://kaikki.org/dictionary/English/kaikki.org-dictionary-English.jsonl

The files are free and openly licensed under CC BY-SA (same as Wikipedia).

---

## Usage

### Basic split

```bash
# German monolingual dictionary → outputs to DE/ folder
python3 prepare_wiktionary.py split kaikki.org-dictionary-German.jsonl --lang de

# English monolingual dictionary → outputs to EN/ folder
python3 prepare_wiktionary.py split kaikki.org-dictionary-English.jsonl --lang en
```

### Custom output folder name

```bash
python3 prepare_wiktionary.py split kaikki.org-dictionary-German.jsonl --lang de --name WIKT-DE
```

### Filter by part of speech

Useful for producing a smaller, faster-to-build output. The full dumps include
abbreviations, character names, phrases, and other low-value entries.

```bash
# Only nouns, verbs, and adjectives
python3 prepare_wiktionary.py split kaikki.org-dictionary-German.jsonl \
    --lang de --pos noun verb adj adv

# Available POS values:
#   noun  verb  adj  adv  prep  conj  pron  article  num  intj
#   particle  prefix  suffix  name  phrase  proverb  abbrev
```

### Limit senses per entry

```bash
# Show at most 3 senses per word (shorter definitions, better for small screen)
python3 prepare_wiktionary.py split kaikki.org-dictionary-German.jsonl \
    --lang de --max-senses 3
```

### Preview statistics without writing anything

```bash
python3 prepare_wiktionary.py stats kaikki.org-dictionary-German.jsonl --lang de
```

Prints entry count, breakdown by part of speech, and entries per letter bucket.

---

## Output Structure

```
DE/
  a.txt    ← all entries whose word starts with A or a (also Ä/ä)
  b.txt
  ...
  z.txt
  0.txt    ← entries starting with a digit
  _.txt    ← entries starting with any other character
```

Umlauts are bucketed into their base letter:
- Ä/ä → a.txt
- Ö/ö → o.txt
- Ü/ü → u.txt
- ß → s.txt

Copy the output folder to your Flipper Zero SD card:

```
/ext/apps_data/fz_dict_app/DE/
```

---

## Definition Format

Each line in a bucket file is:

```
word TAB [pos] (gender) sense1  sense2  sense3
```

Examples:

```
Hund    [Subst.] (mask.) 1. ein Haustier der Familie Canidae  2. (umgangssprachlich) unangenehmer Mensch
laufen  [Verb] 1. sich auf den Beinen fortbewegen  2. in Betrieb sein  3. (Sport) rennen
schön   [Adj.] 1. ästhetisch ansprechend  2. angenehm, erfreulich
```

The app word-wraps the definition in the entry view, so longer definitions
display correctly across multiple lines.

### German POS labels

| Wiktionary POS | Label in app |
|----------------|-------------|
| noun           | Subst.      |
| verb           | Verb        |
| adj            | Adj.        |
| adv            | Adv.        |
| prep           | Präp.       |
| conj           | Konj.       |
| pron           | Pron.       |
| article        | Art.        |
| num            | Num.        |
| intj           | Interj.     |
| name           | Eigenname   |
| phrase         | Phrase      |
| proverb        | Sprichw.    |
| abbrev         | Abk.        |

### English POS labels

| Wiktionary POS | Label in app |
|----------------|-------------|
| noun           | n.          |
| verb           | v.          |
| adj            | adj.        |
| adv            | adv.        |
| prep           | prep.       |
| conj           | conj.       |
| pron           | pron.       |
| name           | proper n.   |
| phrase         | phrase      |
| abbrev         | abbrev.     |

---

## Tips

**Processing time** — the German JSONL takes 1–3 minutes on a modern machine,
English 3–5 minutes. The script prints a dot every 10,000 entries so you can
see progress.

**Recommended settings for Flipper Zero** — the default of 5 senses per entry
works well. If definitions feel too long on the small screen, try
`--max-senses 2` or `--max-senses 3`.

**Combining with dict.cc** — the two dictionary types work side by side. A
practical SD card layout with all four dictionaries:

```
/ext/apps_data/fz_dict_app/
  EN-DE/      ← dict.cc bilingual (prepare_dict.py --bidirectional)
  DE-EN/      ← dict.cc bilingual reverse
  DE/         ← Wiktionary German monolingual (this script)
  EN/         ← Wiktionary English monolingual (this script)
  keywords.txt
```

Switch between them with Left/Right on the Search row in the app.

---

## Attribution

Dictionary data is derived from [Wiktionary](https://www.wiktionary.org/)
via [Kaikki.org](https://kaikki.org/), licensed under
[CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/).
