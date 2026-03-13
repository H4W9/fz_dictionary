# FZ Dictionary

A dict.cc and wiktionary dictionary for Flipper Zero — built in the same style as the FZ Bible App.

---

<img width=19% height=19% alt="main_menu" src="https://github.com/user-attachments/assets/94800bd9-a671-4fbb-b91a-a5f7848d2ef3" />

<img width=19% height=19% alt="settings" src="https://github.com/user-attachments/assets/162a3cab-18a9-44f2-bb43-3724db0d279a" />

<img width=19% height=19% alt="search" src="https://github.com/user-attachments/assets/9327e272-8f42-4ef0-8cc4-ae5f01bf7866" />

<img width=19% height=19% alt="search_results" src="https://github.com/user-attachments/assets/784483a5-5c44-4da1-b831-c74ad56345aa" />

<img width=19% height=19% alt="word_view" src="https://github.com/user-attachments/assets/69c1b767-8ff5-4953-896f-dad6a29d3bc9" />

---

## Features

- **Search** any word in your dict.cc dictionary with a full on-screen keyboard
- **Fast letter-indexed search** — the dictionary is split by first letter, so only 1/26th of the file is read per search
- **Multiple dictionaries** — place multiple language folders in the data directory; switch between them in Settings or from the main menu
- **Favorites** — long-press OK in the entry view to star any entry; view starred entries from the main menu
- **Font system**: 5 selectable font sizes (Default, Tiny, Small, Medium, Large)
- **Dark mode** toggle
- **Keyword suggestions** while typing (load `keywords.txt` on SD card for custom suggestions)
- **Scrollable entry view** — full translation text with word-wrap
- **Navigate results** with Left/Right in the entry view

---

## Getting the Dictionary File

The app uses dictionary exports from [dict.cc](https://www.dict.cc/). You need to download the file yourself — it requires a free account and agreement to their non-commercial terms.

1. Go to **https://www1.dict.cc/translation_file_request.php**
2. Register a free account (or log in if you already have one)
3. Agree to the non-commercial use terms
4. Select your desired language pair (e.g. English–German)
5. Download the `.txt` file to your computer

The downloaded file is a plain-text, tab-separated export — this is exactly what the prep script expects.

---

## SD Card Setup

The app expects dictionaries as **folders of letter-bucket files**, not a single large file. Use the included `prepare_dict.py` script to convert your download.

### Step 1 — Convert the dict.cc file

```bash
# Split into both EN->DE and DE->EN from a single download (recommended):
python3 prepare_dict.py split EN-DE.txt --bidirectional

# Or just one direction:
python3 prepare_dict.py split EN-DE.txt

# Custom folder names for both directions:
python3 prepare_dict.py split EN-DE.txt --bidirectional --name EN-DE --reverse-name DE-EN

# Preview stats without writing anything:
python3 prepare_dict.py stats EN-DE.txt
```

The `--bidirectional` flag reads the file once and writes two folders — the forward direction and a reversed copy with columns swapped. Both appear in the app and you can switch between them with Left/Right on the Search row.

Each folder looks like:

```
EN-DE/
  a.txt   <- all entries whose source word starts with A or a
  b.txt
  ...
  z.txt
  0.txt   <- entries starting with a digit (0-9)
  _.txt   <- entries starting with any other character

DE-EN/    <- created automatically by --bidirectional
  a.txt
  ...
```

### Step 2 — Copy to Flipper Zero SD card

```
/ext/apps_data/fz_dict_app/
  EN-DE/          <- your converted dictionary folder
    a.txt
    b.txt
    ...
  DE-EN/          <- optional: second dictionary (repeat step 1)
    a.txt
    ...
  keywords.txt    <- optional: one word per line for typing suggestions
```

You can have up to 8 dictionary folders. Switch between them using Left/Right on the Search row in the main menu, or via Settings.

### Why letter buckets?

A full dict.cc export is typically 20–40 MB. Searching linearly through that on Flipper Zero hardware would take 15–30 seconds per query. Splitting by first letter means each search reads ~1–2 MB at most — fast enough to feel instant.

---

## Wiktionary Monolingual Dictionaries

You can also build monolingual German or English dictionaries from Wiktionary
using the included `prepare_wiktionary.py` script.

### Step 1 — Download the Kaikki JSONL dump

Go to **https://kaikki.org/dictionary/** and download the file for your language:

- German: `kaikki.org-dictionary-German.jsonl` (~400 MB)
- English: `kaikki.org-dictionary-English.jsonl` (~800 MB)

These are free, openly licensed (CC BY-SA) exports of Wiktionary.

### Step 2 — Convert and split

```bash
# German monolingual dictionary (outputs to DE/ folder)
python3 prepare_wiktionary.py split kaikki.org-dictionary-German.jsonl --lang de

# English monolingual dictionary (outputs to EN/ folder)
python3 prepare_wiktionary.py split kaikki.org-dictionary-English.jsonl --lang en

# Custom folder name
python3 prepare_wiktionary.py split kaikki.org-dictionary-German.jsonl --lang de --name WIKT-DE

# Only nouns, verbs, and adjectives (smaller output, faster to build)
python3 prepare_wiktionary.py split kaikki.org-dictionary-German.jsonl --lang de --pos noun verb adj

# Check stats without writing anything
python3 prepare_wiktionary.py stats kaikki.org-dictionary-German.jsonl --lang de
```

### Output format

Each entry is stored as:

```
word TAB [pos] (gender) sense1  sense2  sense3
```

Example:
```
Hund    [Subst.] (mask.) 1. ein Haustier der Familie Canidae  2. (umgangssprachlich) ein unangenehmer Mensch
```

The app word-wraps the definition in the entry view, so longer definitions display fine.

### Example SD card layout with all dictionaries

```
/ext/apps_data/fz_dict_app/
  EN-DE/      <- dict.cc bilingual (prepare_dict.py --bidirectional)
  DE-EN/      <- dict.cc bilingual reverse
  DE/         <- German Wiktionary monolingual (prepare_wiktionary.py)
  EN/         <- English Wiktionary monolingual (prepare_wiktionary.py)
  keywords.txt
```

Switch between all four with Left/Right on the Search row.

---

## Dictionary File Format

dict.cc exports are tab-separated values:

```
# dict.cc :: English - German dictionary
# Lines starting with # are ignored
source_word[TAB]translation[TAB]optional_notes
```

Example:
```
dog	Hund [m]	[animal]
cat	Katze [f]
run	laufen; rennen
```

---

## Controls

### Main Menu
| Button | Action |
|--------|--------|
| Up/Down | Navigate rows |
| Left/Right on Search row | Cycle active dictionary |
| OK | Open selected view |
| Long-OK | Open Settings |
| Back | Exit app |

### Search (Keyboard)
| Button | Action |
|--------|--------|
| D-Pad | Navigate keyboard |
| OK | Type character / activate button |
| Long-OK (on letter) | Type opposite case |
| Long-Up | Fill suggestion + space |
| Long-L/R | Cycle suggestions |
| Long-Down | Insert space |
| GO! | Search dictionary |
| Back | Backspace |
| Long-Back | Exit to menu |

### Search Results
| Button | Action |
|--------|--------|
| Up/Down | Navigate results |
| OK | Open entry |
| Back | Return to keyboard |

### Entry View
| Button | Action |
|--------|--------|
| Up/Down | Scroll translation text |
| Left | Previous search result |
| Right | Next search result |
| OK | Scroll down one page |
| Long-OK | Toggle favorite (star shown in header) |
| Back | Return to results or previous view |

### Favorites
| Button | Action |
|--------|--------|
| Up/Down | Navigate list |
| OK | Open entry |
| Long-OK | Remove from favorites |
| Back | Return to menu |

### History
| Button | Action |
|--------|--------|
| Up/Down | Navigate list |
| OK | Fill search bar with term and go to keyboard |
| Long-OK | Delete selected entry |
| Long-Back | Clear all history (shows confirmation prompt) |
| Back | Return to menu |

### Settings
| Button | Action |
|--------|--------|
| Up/Down | Navigate rows |
| Left/Right | Toggle / cycle value |
| Long-OK | Open expanded list picker |
| OK | Save and close |
| Back | Close without saving |

---

## Building

Place this directory alongside your Flipper Zero firmware and build with:

```bash
./fbt fap_fz_dict_app
```

Or use the GitHub Actions workflow (see `.github/workflows/build.yml` from FZ Bible for reference).

---

## Acknowledgements

Built on the architecture of the [FZ Bible App](https://github.com/H4W9/FZ_Bible_App), preserving the same UI patterns, keyboard, font system, and code style.

Dictionary data from [dict.cc](https://www.dict.cc/) — please respect their terms of service when downloading and using their data.
