// FZ Dictionary - dict.cc Dictionary Viewer for Flipper Zero
// SD card: /ext/apps_data/fz_dict_app/<dictionary>.txt
//
// Dictionary format (dict.cc export):
//   Lines starting with '#' are ignored.
//   Each entry line: source_word\ttranslation[\ttype_notes]
//
// Multiple .txt files in the data directory are detected as separate
// dictionaries, selectable in Settings (same pattern as FZ Bible translations).

#define APP_VERSION "1.0"
#define APP_NAME    "FZ Dictionary"

#include "font/font.h"
#include "fz_dict.h"
#include "keyboard/keyboard.h"
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <storage/storage.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================
// Font tables (same sizes as FZ Bible)
// ============================================================

static const uint8_t FONT_CHARS[FONT_COUNT]  = { 22, 30, 24, 20, 13 };
static const uint8_t FONT_LINE_H[FONT_COUNT] = { 10,  8, 10, 12, 16 };
static const char* const FONT_LABELS[FONT_COUNT] = {
    "Default (built-in)",
    "Tiny    (4x6)",
    "Small   (5x8)",
    "Medium  (6x10)",
    "Large   (9x15)",
};

// ============================================================
// Color helpers
// ============================================================

static void set_bg(Canvas* canvas, App* app) {
    if(app->dark_mode) canvas_set_color(canvas, ColorBlack);
    else               canvas_set_color(canvas, ColorWhite);
}
void set_fg(Canvas* canvas, App* app) {
    if(app->dark_mode) canvas_set_color(canvas, ColorWhite);
    else               canvas_set_color(canvas, ColorBlack);
}

// ============================================================
// Font helpers
// ============================================================

static void apply_font(Canvas* canvas, FontChoice f) {
    switch(f) {
    case FontSmall:  canvas_set_font_custom(canvas, FONT_SIZE_SMALL);  break;
    case FontMedium: canvas_set_font_custom(canvas, FONT_SIZE_MEDIUM); break;
    case FontLarge:  canvas_set_font_custom(canvas, FONT_SIZE_LARGE);  break;
    case FontXLarge: canvas_set_font_custom(canvas, FONT_SIZE_XLARGE); break;
    default:         canvas_set_font(canvas, FontSecondary);           break;
    }
}

static uint8_t font_visible_lines(FontChoice f) {
    return (uint8_t)((SCREEN_H - READ_BODY_Y) / FONT_LINE_H[f]);
}

// set_ui_font: fall back to custom font when the string contains umlauts
// (called by keyboard.c for search results rendering)
void set_ui_font(Canvas* canvas, const char* str) {
    if(str_has_umlaut(str))
        canvas_set_font_custom(canvas, UMLAUT_FALLBACK_FONT);
    else
        canvas_set_font(canvas, FontSecondary);
}

// ============================================================
// Settings persistence
// ============================================================

static void settings_save(App* app) {
    File* f = storage_file_alloc(app->storage);
    if(!storage_file_open(f, SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_free(f);
        return;
    }
    char buf[96];
    int len = snprintf(buf, sizeof(buf),
        "font=%d\ndark=%d\ndict=%d\n",
        (int)app->font_choice,
        (int)app->dark_mode,
        (int)app->dict_idx);
    if(len > 0) storage_file_write(f, buf, (uint16_t)len);
    storage_file_close(f);
    storage_file_free(f);
}

static void settings_load(App* app) {
    app->font_choice = FontSecondaryBuiltin;
    app->dark_mode   = false;
    app->dict_idx    = 0;

    File* f = storage_file_alloc(app->storage);
    if(!storage_file_open(f, SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(f);
        return;
    }
    char buf[96];
    uint16_t rd = storage_file_read(f, buf, sizeof(buf) - 1);
    buf[rd] = '\0';
    storage_file_close(f);
    storage_file_free(f);

    char* p;
    if((p = strstr(buf, "font=")) != NULL) { int v = atoi(p+5); if(v>=0&&v<FONT_COUNT) app->font_choice=(FontChoice)v; }
    if((p = strstr(buf, "dark=")) != NULL) { app->dark_mode = (atoi(p+5) != 0); }
    if((p = strstr(buf, "dict=")) != NULL) { int v = atoi(p+5); if(v>=0&&v<MAX_DICTS) app->dict_idx=(uint8_t)v; }
}

// ============================================================
// Favorites persistence
// ============================================================

static void favorites_save(App* app) {
    File* f = storage_file_alloc(app->storage);
    if(!storage_file_open(f, FAVORITES_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_free(f);
        return;
    }
    for(uint8_t i = 0; i < app->fav_count; i++) {
        char line[FAV_WORD_LEN + 24];
        int len = snprintf(line, sizeof(line), "%u %u %s\n",
            (unsigned)app->favorites[i].file_offset,
            (unsigned)app->favorites[i].dict_idx,
            app->favorites[i].word);
        if(len > 0) storage_file_write(f, line, (uint16_t)len);
    }
    storage_file_close(f);
    storage_file_free(f);
}

static void favorites_load(App* app) {
    app->fav_count  = 0;
    app->fav_sel    = 0;
    app->fav_scroll = 0;

    File* f = storage_file_alloc(app->storage);
    if(!storage_file_open(f, FAVORITES_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(f);
        return;
    }
    char* buf = malloc(4096);
    if(!buf) { storage_file_free(f); return; }
    uint16_t rd = storage_file_read(f, buf, 4095);
    buf[rd] = '\0';
    storage_file_close(f);
    storage_file_free(f);

    char* line = buf;
    while(*line && app->fav_count < MAX_FAVORITES) {
        unsigned offset = 0, didx = 0;
        char word[FAV_WORD_LEN];
        word[0] = '\0';
        int fields = sscanf(line, "%u %u %47[^\n]", &offset, &didx, word);
        if(fields >= 3 && word[0]) {
            app->favorites[app->fav_count].file_offset = (uint32_t)offset;
            app->favorites[app->fav_count].dict_idx    = (uint8_t)(didx < MAX_DICTS ? didx : 0);
            strncpy(app->favorites[app->fav_count].word, word, FAV_WORD_LEN - 1);
            app->favorites[app->fav_count].word[FAV_WORD_LEN - 1] = '\0';
            // Trim trailing whitespace from word
            size_t wl = strlen(app->favorites[app->fav_count].word);
            while(wl > 0 && (app->favorites[app->fav_count].word[wl-1] == '\r' ||
                              app->favorites[app->fav_count].word[wl-1] == ' '))
                app->favorites[app->fav_count].word[--wl] = '\0';
            app->fav_count++;
        }
        while(*line && *line != '\n') line++;
        if(*line == '\n') line++;
    }
    free(buf);
}

// ============================================================
// Dictionary detection (scan DATA_DIR for subdirectories containing
// letter-bucket files: a.txt, b.txt, … z.txt, 0.txt, _.txt)
//
// SD card layout:
//   /ext/apps_data/fz_dict_app/
//     <DictName>/          ← one subdirectory per dictionary
//       a.txt              ← all entries whose source word starts with A/a
//       b.txt
//       ...
//       z.txt
//       0.txt              ← entries starting with a digit (0-9)
//       _.txt              ← entries starting with any other character
// ============================================================

// Build the path to the letter-bucket file for a given first character
// within the currently selected dictionary.
// 'A'-'Z' map to a.txt-z.txt (case-normalised); '0'-'9' → 0.txt; else → _.txt
static void letter_file_path(App* app, char first_char, char* buf, size_t len) {
    char lc = first_char;
    if(lc >= 'A' && lc <= 'Z') lc = (char)(lc + 32);

    char bucket[8];
    if     (lc >= 'a' && lc <= 'z') snprintf(bucket, sizeof(bucket), "%c.txt", lc);
    else if(lc >= '0' && lc <= '9') snprintf(bucket, sizeof(bucket), "0.txt");
    else                             snprintf(bucket, sizeof(bucket), "_.txt");

    snprintf(buf, len, "%s/%s/%s", DATA_DIR, app->dicts[app->dict_idx], bucket);
}

// Probe whether a candidate subdirectory looks like a valid letter-split
// dictionary by checking for the existence of at least one letter file.
static bool subdir_is_dict(App* app, const char* dirname) {
    // Check a handful of common letters; any one present is enough.
    static const char* const probes[] = { "a.txt", "b.txt", "s.txt", "0.txt" };
    for(uint8_t i = 0; i < 4; i++) {
        char path[200];
        snprintf(path, sizeof(path), "%s/%s/%s", DATA_DIR, dirname, probes[i]);
        File* f = storage_file_alloc(app->storage);
        bool found = storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING);
        if(found) storage_file_close(f);
        storage_file_free(f);
        if(found) return true;
    }
    return false;
}

static void dicts_scan(App* app) {
    app->dict_count = 0;
    app->dict_idx   = 0;

    File* dir = storage_file_alloc(app->storage);
    if(!storage_dir_open(dir, DATA_DIR)) {
        storage_file_free(dir);
        return;
    }

    FileInfo fi;
    char fname[DICT_NAME_LEN];
    while(storage_dir_read(dir, &fi, fname, sizeof(fname))) {
        if(!file_info_is_dir(&fi)) continue;   // only subdirectories
        if(fname[0] == '.') continue;

        if(subdir_is_dict(app, fname) && app->dict_count < MAX_DICTS) {
            strncpy(app->dicts[app->dict_count], fname, DICT_NAME_LEN - 1);
            app->dicts[app->dict_count][DICT_NAME_LEN - 1] = '\0';
            app->dict_count++;
        }
    }

    storage_dir_close(dir);
    storage_file_free(dir);
}

// ============================================================
// Keywords / suggestions
// ============================================================

static char s_kw_words[MAX_KEYWORDS][KEYWORD_WORD_LEN];
static uint16_t s_kw_count = 0;

void keywords_load(App* app) {
    s_kw_count         = 0;
    app->kw_count      = 0;
    app->suggest_count = 0;
    app->suggest_sel   = 0;

    File* f = storage_file_alloc(app->storage);
    bool from_file = storage_file_open(f, KEYWORDS_PATH_DICT, FSAM_READ, FSOM_OPEN_EXISTING);
    if(from_file) {
        char line[KEYWORD_WORD_LEN + 2];
        uint8_t lpos = 0;
        char ch = 0;
        while(s_kw_count < MAX_KEYWORDS) {
            uint16_t rd = storage_file_read(f, &ch, 1);
            if(rd == 0) {
                if(lpos > 0) {
                    while(lpos > 0 && line[lpos-1] == ' ') lpos--;
                    if(lpos > 0) {
                        line[lpos] = '\0';
                        memcpy(s_kw_words[s_kw_count++], line, lpos + 1);
                    }
                }
                break;
            }
            if(ch == '\n' || ch == '\r') {
                while(lpos > 0 && line[lpos-1] == ' ') lpos--;
                if(lpos > 0) {
                    line[lpos] = '\0';
                    memcpy(s_kw_words[s_kw_count++], line, lpos + 1);
                }
                lpos = 0;
            } else {
                if(lpos < KEYWORD_WORD_LEN - 1) line[lpos++] = ch;
            }
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    app->kw_count = s_kw_count;
}

static bool kw_prefix_match(const char* word, const char* prefix, uint8_t plen) {
    for(uint8_t i = 0; i < plen; i++) {
        char a = word[i], b = prefix[i];
        if(!a) return false;
        if(a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if(b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if(a != b) return false;
    }
    return true;
}

void suggestions_update(App* app) {
    app->suggest_count = 0;
    if(app->search_len == 0) return;

    const char* prefix = app->search_buf;
    for(int i = (int)app->search_len - 1; i >= 0; i--) {
        if(app->search_buf[i] == ' ') { prefix = app->search_buf + i + 1; break; }
    }
    uint8_t plen = (uint8_t)strlen(prefix);
    if(plen == 0) return;

    for(uint16_t i = 0; i < s_kw_count && app->suggest_count < SUGGEST_MAX; i++) {
        if(kw_prefix_match(s_kw_words[i], prefix, plen)) {
            strncpy(app->suggest[app->suggest_count], s_kw_words[i], KEYWORD_WORD_LEN - 1);
            app->suggest[app->suggest_count][KEYWORD_WORD_LEN - 1] = '\0';
            app->suggest_count++;
        }
    }
    if(app->suggest_sel >= app->suggest_count)
        app->suggest_sel = (app->suggest_count > 0) ? app->suggest_count - 1 : 0;
}

void suggestion_fill(App* app) {
    if(app->suggest_count == 0) return;
    const char* word = app->suggest[app->suggest_sel];

    int last_space = -1;
    for(int i = (int)app->search_len - 1; i >= 0; i--) {
        if(app->search_buf[i] == ' ') { last_space = i; break; }
    }
    uint8_t prefix_end = (uint8_t)(last_space + 1);
    size_t wlen = strlen(word);
    if(prefix_end + wlen >= MAX_SEARCH_LEN) return;

    memcpy(app->search_buf + prefix_end, word, wlen);
    app->search_len = (uint8_t)(prefix_end + wlen);
    app->search_buf[app->search_len] = '\0';

    app->suggest_count = 0;
    app->suggest_sel   = 0;
    app->cursor_pos    = app->search_len;
}

// ============================================================
// Dictionary search (chunked streaming)
// ============================================================

// Case-insensitive substring match (handles ASCII; UTF-8 works for byte identity)
static bool icontains_ascii(const char* hay, const char* needle) {
    if(!hay || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(hay);
    if(nlen > hlen) return false;
    for(size_t i = 0; i <= hlen - nlen; i++) {
        bool ok = true;
        for(size_t j = 0; j < nlen; j++) {
            char a = hay[i+j], b = needle[j];
            if(a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if(b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if(a != b) { ok = false; break; }
        }
        if(ok) return true;
    }
    return false;
}

// Truncate a display string to max_chars visible characters (UTF-8 aware)
static void truncate_utf8_display(const char* src, char* dst, size_t dst_size, uint8_t max_chars) {
    size_t si = 0, di = 0;
    uint8_t chars = 0;
    while(src[si] && chars < max_chars && di < dst_size - 4) {
        dst[di++] = src[si++];
        while(src[si] && ((uint8_t)src[si] & 0xC0) == 0x80 && di < dst_size - 1)
            dst[di++] = src[si++];
        chars++;
    }
    dst[di] = '\0';
}

// Process one dict file line; add to hits[] if source word matches search_buf
static void process_search_line(App* app, const char* line, uint32_t offset) {
    if(!line[0] || line[0] == '#') return;

    const char* tab1 = strchr(line, '\t');
    if(!tab1) return;

    size_t src_len = (size_t)(tab1 - line);
    if(src_len == 0) return;

    // Build source word string
    char source[LINE_BUF_LEN];
    if(src_len >= sizeof(source)) src_len = sizeof(source) - 1;
    memcpy(source, line, src_len);
    source[src_len] = '\0';

    if(!icontains_ascii(source, app->search_buf)) return;

    uint8_t hi = app->hit_count;
    app->hits[hi].file_offset = offset;

    // Word for display (truncated to WORD_DISPLAY_LEN)
    strncpy(app->hits[hi].word, source, WORD_DISPLAY_LEN - 1);
    app->hits[hi].word[WORD_DISPLAY_LEN - 1] = '\0';

    // Translation preview: text between first and second tab (or end of line)
    const char* trans_start = tab1 + 1;
    const char* tab2 = strchr(trans_start, '\t');
    size_t trans_len = tab2 ? (size_t)(tab2 - trans_start) : strlen(trans_start);
    if(trans_len >= TRANS_DISPLAY_LEN) trans_len = TRANS_DISPLAY_LEN - 1;
    memcpy(app->hits[hi].trans, trans_start, trans_len);
    app->hits[hi].trans[trans_len] = '\0';

    // Build the display ref shown in the results list:
    // "word_truncated" on the left; fits in ~20 chars of FontSecondary on 128px screen.
    // Format: source word (max 18 chars) so there's room to read it
    char word_short[24];
    truncate_utf8_display(app->hits[hi].word, word_short, sizeof(word_short), 18);
    snprintf(app->hits[hi].ref, HIT_REF_LEN, "%s", word_short);

    app->hit_count++;
}

void do_search(App* app) {
    app->hit_count  = 0;
    app->hit_sel    = 0;
    app->hit_scroll = 0;

    if(!app->search_len || app->dict_count == 0) return;

    // Determine which letter-bucket file to search.
    // We use the first character of the query: searching "dog" only
    // opens d.txt, "Haus" only h.txt, etc.
    char letter_path[220];
    letter_file_path(app, app->search_buf[0], letter_path, sizeof(letter_path));

    File* f = storage_file_alloc(app->storage);
    if(!storage_file_open(f, letter_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(f);
        return;
    }

    char chunk[SEARCH_CHUNK_SIZE];
    char line_buf[LINE_BUF_LEN];
    uint16_t lpos = 0;
    uint32_t file_offset = 0;
    uint32_t line_start  = 0;

    while(app->hit_count < MAX_SEARCH_HITS) {
        uint16_t rd = storage_file_read(f, chunk, SEARCH_CHUNK_SIZE);
        if(rd == 0) {
            // Flush final line if any
            if(lpos > 0) {
                line_buf[lpos] = '\0';
                process_search_line(app, line_buf, line_start);
            }
            break;
        }

        for(uint16_t i = 0; i < rd && app->hit_count < MAX_SEARCH_HITS; i++) {
            char ch = chunk[i];
            if(ch == '\n') {
                line_buf[lpos] = '\0';
                if(lpos > 0) process_search_line(app, line_buf, line_start);
                lpos = 0;
                file_offset++;
                line_start = file_offset;
            } else if(ch == '\r') {
                file_offset++;
            } else {
                if(lpos < LINE_BUF_LEN - 1) line_buf[lpos++] = ch;
                file_offset++;
            }
        }

        if(rd < SEARCH_CHUNK_SIZE) {
            if(lpos > 0 && app->hit_count < MAX_SEARCH_HITS) {
                line_buf[lpos] = '\0';
                process_search_line(app, line_buf, line_start);
                lpos = 0;
            }
            break;
        }
    }

    storage_file_close(f);
    storage_file_free(f);
}

// ============================================================
// Entry (reading) view: load a hit and wrap for display
// ============================================================

static void wrap_entry(App* app) {
    memset(&app->wrap, 0, sizeof(app->wrap));
    uint8_t cols = FONT_CHARS[app->font_choice];
    if(cols > 2) cols = (uint8_t)(cols - 1);

    const char* src = app->entry_text;
    size_t src_len = strlen(src);
    size_t pos = 0;
    uint16_t line = 0;

    while(pos < src_len && line < WRAP_MAX_LINES) {
        size_t rem  = src_len - pos;
        size_t take = (rem < cols) ? rem : cols;

        // Try to break on a space
        if(pos + take < src_len && src[pos + take] != ' ') {
            size_t t2 = take;
            while(t2 > 0 && src[pos + t2] != ' ') t2--;
            if(t2 > 0) take = t2;
        }

        size_t copy = (take < 38u) ? take : 38u;
        memcpy(app->wrap.lines[line], src + pos, copy);
        app->wrap.lines[line][copy] = '\0';

        pos += take;
        if(pos < src_len && src[pos] == ' ') pos++;
        line++;
    }

    app->wrap.count  = line;
    app->wrap.scroll = 0;
}

// Check if the current entry (by file_offset + dict_idx) is in favorites
static bool entry_in_favorites(App* app, uint32_t offset) {
    for(uint8_t i = 0; i < app->fav_count; i++) {
        if(app->favorites[i].file_offset == offset &&
           app->favorites[i].dict_idx    == app->dict_idx)
            return true;
    }
    return false;
}

// Load the entry at hits[hit_sel] from the dict file, wrap it, switch to ViewEntry.
// The correct letter-bucket file is derived from the source word already stored in
// hits[hi].word, so no extra data needs to be carried in the hit struct.
void open_entry(App* app) {
    uint8_t hi = app->hit_sel;
    if(hi >= app->hit_count) return;
    if(app->dict_count == 0) return;

    // Derive letter-bucket file from the stored source word
    char letter_path[220];
    letter_file_path(app, app->hits[hi].word[0], letter_path, sizeof(letter_path));

    char line_buf[LINE_BUF_LEN];
    line_buf[0] = '\0';

    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, letter_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        if(storage_file_seek(f, app->hits[hi].file_offset, true)) {
            uint16_t rd = storage_file_read(f, line_buf, LINE_BUF_LEN - 1);
            line_buf[rd] = '\0';
            char* nl = strchr(line_buf, '\n');
            if(nl) *nl = '\0';
            char* cr = strchr(line_buf, '\r');
            if(cr) *cr = '\0';
        }
        storage_file_close(f);
    }
    storage_file_free(f);

    // Parse: source_word \t translation [\t type_notes]
    char* tab1 = strchr(line_buf, '\t');
    if(tab1) {
        // Source word → entry header
        size_t src_len = (size_t)(tab1 - line_buf);
        if(src_len >= ENTRY_WORD_LEN) src_len = ENTRY_WORD_LEN - 1;
        memcpy(app->entry_word, line_buf, src_len);
        app->entry_word[src_len] = '\0';

        // Translation text (keep second tab as space for readability)
        const char* trans_start = tab1 + 1;
        strncpy(app->entry_text, trans_start, ENTRY_TEXT_LEN - 1);
        app->entry_text[ENTRY_TEXT_LEN - 1] = '\0';
        // Replace remaining tabs with spaces
        for(char* p = app->entry_text; *p; p++)
            if(*p == '\t') *p = ' ';
    } else {
        // No tab — show entire line
        strncpy(app->entry_word, line_buf, ENTRY_WORD_LEN - 1);
        app->entry_word[ENTRY_WORD_LEN - 1] = '\0';
        app->entry_text[0] = '\0';
    }

    app->entry_is_fav = entry_in_favorites(app, app->hits[hi].file_offset);

    app->view = ViewLoading;
    view_port_update(app->view_port);
    wrap_entry(app);
    app->wrap.scroll = 0;
    app->prev_view   = ViewSearchResults;
    app->view        = ViewEntry;
}

// Open a favorites entry (direct jump via stored offset).
// The letter-bucket file is derived from favorites[fi].word[0].
static void open_entry_from_fav(App* app) {
    uint8_t fi = app->fav_sel;
    if(fi >= app->fav_count) return;
    if(app->dict_count == 0) return;

    // Restore dictionary context
    uint8_t saved_dict = app->favorites[fi].dict_idx;
    if(saved_dict < app->dict_count) app->dict_idx = saved_dict;

    // Derive letter-bucket file from stored word
    char letter_path[220];
    letter_file_path(app, app->favorites[fi].word[0], letter_path, sizeof(letter_path));

    char line_buf[LINE_BUF_LEN];
    line_buf[0] = '\0';

    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, letter_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        if(storage_file_seek(f, app->favorites[fi].file_offset, true)) {
            uint16_t rd = storage_file_read(f, line_buf, LINE_BUF_LEN - 1);
            line_buf[rd] = '\0';
            char* nl = strchr(line_buf, '\n');
            if(nl) *nl = '\0';
            char* cr = strchr(line_buf, '\r');
            if(cr) *cr = '\0';
        }
        storage_file_close(f);
    }
    storage_file_free(f);

    char* tab1 = strchr(line_buf, '\t');
    if(tab1) {
        size_t src_len = (size_t)(tab1 - line_buf);
        if(src_len >= ENTRY_WORD_LEN) src_len = ENTRY_WORD_LEN - 1;
        memcpy(app->entry_word, line_buf, src_len);
        app->entry_word[src_len] = '\0';
        const char* trans_start = tab1 + 1;
        strncpy(app->entry_text, trans_start, ENTRY_TEXT_LEN - 1);
        app->entry_text[ENTRY_TEXT_LEN - 1] = '\0';
        for(char* p = app->entry_text; *p; p++)
            if(*p == '\t') *p = ' ';
    } else {
        strncpy(app->entry_word, line_buf, ENTRY_WORD_LEN - 1);
        app->entry_word[ENTRY_WORD_LEN - 1] = '\0';
        app->entry_text[0] = '\0';
    }

    app->entry_is_fav = true;
    // Create a synthetic single hit so Left/Right navigation still works
    app->hits[0].file_offset = app->favorites[fi].file_offset;
    strncpy(app->hits[0].word, app->entry_word, WORD_DISPLAY_LEN - 1);
    app->hits[0].word[WORD_DISPLAY_LEN - 1] = '\0';
    app->hits[0].ref[0] = '\0';
    app->hit_count = 1;
    app->hit_sel   = 0;

    app->view = ViewLoading;
    view_port_update(app->view_port);
    wrap_entry(app);
    app->wrap.scroll = 0;
    app->prev_view   = ViewFavorites;
    app->view        = ViewEntry;
}

// ============================================================
// Favorites toggle
// ============================================================

static void toggle_favorite(App* app) {
    if(app->hit_count == 0) return;
    uint8_t hi = app->hit_sel;
    uint32_t offset = app->hits[hi].file_offset;

    // Check if already in favorites
    for(uint8_t i = 0; i < app->fav_count; i++) {
        if(app->favorites[i].file_offset == offset &&
           app->favorites[i].dict_idx    == app->dict_idx) {
            // Remove
            for(uint8_t j = i; j + 1 < app->fav_count; j++)
                app->favorites[j] = app->favorites[j + 1];
            app->fav_count--;
            app->entry_is_fav = false;
            favorites_save(app);
            return;
        }
    }

    // Add
    if(app->fav_count >= MAX_FAVORITES) return;
    app->favorites[app->fav_count].file_offset = offset;
    app->favorites[app->fav_count].dict_idx    = app->dict_idx;
    strncpy(app->favorites[app->fav_count].word, app->entry_word, FAV_WORD_LEN - 1);
    app->favorites[app->fav_count].word[FAV_WORD_LEN - 1] = '\0';
    app->fav_count++;
    app->entry_is_fav = true;
    favorites_save(app);
}

// ============================================================
// Shared draw primitives
// ============================================================

void draw_hdr(Canvas* canvas, const char* title) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, SCREEN_W, HDR_H);
    canvas_set_color(canvas, ColorWhite);
    if(str_has_umlaut(title)) {
        canvas_set_font_custom(canvas, UMLAUT_FALLBACK_FONT_HDR);
    } else {
        canvas_set_font(canvas, FontPrimary);
    }
    canvas_draw_str_aligned(canvas, SCREEN_W / 2, 1,
                            AlignCenter, AlignTop, title);
    canvas_set_color(canvas, ColorBlack);
}

void draw_scrollbar(Canvas* canvas, App* app,
                    uint16_t pos, uint16_t total, uint8_t vis) {
    if(total <= vis) return;
    uint8_t track_h = (uint8_t)(SCREEN_H - HDR_H - 2);
    uint8_t bar_h   = (uint8_t)((uint32_t)track_h * vis / total);
    if(bar_h < 3) bar_h = 3;
    uint8_t bar_y = (uint8_t)(HDR_H + 1 +
        (uint32_t)(track_h - bar_h) * pos / (total - vis));
    set_fg(canvas, app);
    canvas_draw_line(canvas, SB_X + 1, HDR_H + 1, SB_X + 1, SCREEN_H - 1);
    canvas_draw_box(canvas, SB_X, bar_y, SB_W, bar_h);
}

// ============================================================
// Scene: Menu
// ============================================================

static void draw_menu(Canvas* canvas, App* app) {
    if(app->dark_mode) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, SCREEN_W, SCREEN_H);
    }
    draw_hdr(canvas, APP_NAME);

    set_fg(canvas, app);
    canvas_set_font(canvas, FontSecondary);

    // Keep scroll in range
    if((uint8_t)app->sel_row < app->menu_scroll)
        app->menu_scroll = (uint8_t)app->sel_row;
    if((uint8_t)app->sel_row >= app->menu_scroll + MENU_VIS)
        app->menu_scroll = (uint8_t)app->sel_row - MENU_VIS + 1;

    for(uint8_t vi = 0; vi < MENU_VIS; vi++) {
        uint8_t r = app->menu_scroll + vi;
        if(r >= MENU_ROWS) break;

        int y = MENU_BODY_Y + vi * MENU_ROW_H;
        bool sel = ((uint8_t)app->sel_row == r);

        if(sel) {
            set_fg(canvas, app);
            canvas_draw_box(canvas, 0, y, SCREEN_W - SB_W - 2, MENU_ROW_H);
            set_bg(canvas, app);
        } else {
            set_fg(canvas, app);
        }

        // Divider above Settings row
        if(r == RowSettings && !sel) {
            set_fg(canvas, app);
            canvas_draw_line(canvas, 0, y - 1, SCREEN_W - SB_W - 3, y - 1);
        }

        switch(r) {
        case RowSearch:
            canvas_draw_str(canvas, 2, y + 8, "Search");
            if(app->dict_count == 0) {
                canvas_draw_str_aligned(canvas, SCREEN_W - SB_W - 3, y + 8,
                                        AlignRight, AlignBottom, "No dict!");
            } else {
                // Show active dictionary folder name (max 14 chars to fit)
                char val[20];
                snprintf(val, sizeof(val), "<%.14s>", app->dicts[app->dict_idx]);
                canvas_draw_str_aligned(canvas, SCREEN_W - SB_W - 3, y + 8,
                                        AlignRight, AlignBottom, val);
            }
            break;
        case RowFavorites: {
            canvas_draw_str(canvas, 2, y + 8, "Favorites");
            char fval[10];
            if(app->fav_count == 0)
                snprintf(fval, sizeof(fval), ">");
            else
                snprintf(fval, sizeof(fval), "%d >", (int)app->fav_count);
            canvas_draw_str_aligned(canvas, SCREEN_W - SB_W - 3, y + 8,
                                    AlignRight, AlignBottom, fval);
            break;
        }
        case RowSettings:
            canvas_draw_str(canvas, 2, y + 8, "Settings");
            canvas_draw_str_aligned(canvas, SCREEN_W - SB_W - 3, y + 8,
                                    AlignRight, AlignBottom, ">");
            break;
        case RowAbout:
            canvas_draw_str(canvas, 2, y + 8, "About");
            canvas_draw_str_aligned(canvas, SCREEN_W - SB_W - 3, y + 8,
                                    AlignRight, AlignBottom, ">");
            break;
        default: break;
        }

        set_fg(canvas, app);
    }

    draw_scrollbar(canvas, app, app->menu_scroll, MENU_ROWS, MENU_VIS);
}

// ============================================================
// Scene: Entry (Reading) view
// ============================================================

static void draw_entry(Canvas* canvas, App* app) {
    if(app->dark_mode) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, SCREEN_W, SCREEN_H);
    }

    // Header: source word (truncated if needed)
    char hdr[40];
    truncate_utf8_display(app->entry_word, hdr, sizeof(hdr), 18);
    draw_hdr(canvas, hdr);

    // Favorite star indicator in header
    if(app->entry_is_fav) {
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, SCREEN_W - 2, 1,
                                AlignRight, AlignTop, "*");
        canvas_set_color(canvas, ColorBlack);
    }

    // Navigation hints: < when more results left, > when more right
    if(app->hit_count > 1) {
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontSecondary);
        if(app->hit_sel > 0)
            canvas_draw_str(canvas, 1, HDR_H - 2, "<");
        if(app->hit_sel < app->hit_count - 1)
            canvas_draw_str_aligned(canvas, SCREEN_W - 2, HDR_H - 2,
                                    AlignRight, AlignBottom, ">");
        canvas_set_color(canvas, ColorBlack);
    }

    set_fg(canvas, app);
    apply_font(canvas, app->font_choice);
    uint8_t lh  = FONT_LINE_H[app->font_choice];
    uint8_t vis = font_visible_lines(app->font_choice);

    for(uint8_t i = 0; i < vis &&
            (app->wrap.scroll + i) < app->wrap.count; i++) {
        canvas_draw_str(canvas, 2,
                        READ_BODY_Y + i * lh + lh - 1,
                        app->wrap.lines[app->wrap.scroll + i]);
    }

    draw_scrollbar(canvas, app, app->wrap.scroll, app->wrap.count, vis);
}

// ============================================================
// Scene: Settings
// ============================================================

static void draw_settings(Canvas* canvas, App* app) {
    if(app->dark_mode) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, SCREEN_W, SCREEN_H);
    }
    draw_hdr(canvas, "Settings");

    canvas_set_font(canvas, FontSecondary);
    const uint8_t ITEM_H = 11;
    const uint8_t BODY_Y = HDR_H + 2;
    const uint8_t VIS    = 4;

    // -- Expanded font picker --
    if(app->settings_font_open) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, HDR_H, SCREEN_W, 9);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, SCREEN_W / 2, HDR_H + 8,
                                AlignCenter, AlignBottom, "Select Font  OK=Confirm");
        set_fg(canvas, app);

        uint8_t scroll = (app->settings_font_sel >= VIS) ?
                         app->settings_font_sel - VIS + 1 : 0;
        const uint8_t FY0 = HDR_H + 11;
        for(uint8_t i = 0; i < VIS && (scroll + i) < FONT_COUNT; i++) {
            uint8_t si     = scroll + i;
            uint8_t y      = FY0 + i * ITEM_H;
            bool    cursor = (app->settings_font_sel == si);
            bool    active = ((uint8_t)app->font_choice == si);
            if(cursor) {
                set_fg(canvas, app);
                canvas_draw_box(canvas, 2, y - 1, SCREEN_W - 4, ITEM_H);
                set_bg(canvas, app);
            } else {
                set_fg(canvas, app);
            }
            canvas_draw_str(canvas, 5,  y + 8, active ? ">" : " ");
            canvas_draw_str(canvas, 13, y + 8, FONT_LABELS[si]);
            set_fg(canvas, app);
        }
        draw_scrollbar(canvas, app, scroll, FONT_COUNT, VIS);
        return;
    }

    // -- Expanded dictionary picker --
    if(app->settings_dict_open) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, HDR_H, SCREEN_W, 9);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, SCREEN_W / 2, HDR_H + 8,
                                AlignCenter, AlignBottom, "Select Dict  OK=Confirm");
        set_fg(canvas, app);

        if(app->dict_count == 0) {
            canvas_draw_str_aligned(canvas, SCREEN_W / 2, SCREEN_H / 2,
                                    AlignCenter, AlignCenter, "No dict found");
            return;
        }

        uint8_t scroll = (app->settings_dict_sel >= VIS) ?
                         app->settings_dict_sel - VIS + 1 : 0;
        const uint8_t DY0 = HDR_H + 11;
        for(uint8_t i = 0; i < VIS && (scroll + i) < app->dict_count; i++) {
            uint8_t si     = scroll + i;
            uint8_t y      = DY0 + i * ITEM_H;
            bool    cursor = (app->settings_dict_sel == si);
            bool    active = (app->dict_idx == si);
            if(cursor) {
                set_fg(canvas, app);
                canvas_draw_box(canvas, 2, y - 1, SCREEN_W - 4, ITEM_H);
                set_bg(canvas, app);
            } else {
                set_fg(canvas, app);
            }
            canvas_draw_str(canvas, 5,  y + 8, active ? ">" : " ");
            canvas_draw_str(canvas, 13, y + 8, app->dicts[si]);
            set_fg(canvas, app);
        }
        draw_scrollbar(canvas, app, scroll, app->dict_count, VIS);
        return;
    }

    // -- Collapsed view --

    // Row 0: Dictionary (shown when multiple dicts available)
    if(app->dict_count > 1) {
        uint8_t y   = BODY_Y;
        bool    sel = (app->settings_sel == SettingsRowDict);
        if(sel) {
            set_fg(canvas, app);
            canvas_draw_box(canvas, 0, y, SCREEN_W, ITEM_H);
            set_bg(canvas, app);
        } else {
            set_fg(canvas, app);
        }
        canvas_draw_str(canvas, 3, y + 8, "Dictionary");
        char tval[20];
        snprintf(tval, sizeof(tval), "[%.14s]", app->dicts[app->dict_idx]);
        canvas_draw_str_aligned(canvas, SCREEN_W - 2, y + 8,
                                AlignRight, AlignBottom, tval);
        set_fg(canvas, app);
    }

    // Row: Font
    {
        uint8_t row_offset = (app->dict_count > 1) ? 1 : 0;
        uint8_t y   = BODY_Y + ITEM_H * row_offset;
        bool    sel = (app->settings_sel == SettingsRowFont);
        if(sel) {
            set_fg(canvas, app);
            canvas_draw_box(canvas, 0, y, SCREEN_W, ITEM_H);
            set_bg(canvas, app);
        } else {
            set_fg(canvas, app);
        }
        canvas_draw_str(canvas, 3, y + 8, "Font");
        char fval[32];
        snprintf(fval, sizeof(fval), "[%s]", FONT_LABELS[app->font_choice]);
        canvas_draw_str_aligned(canvas, SCREEN_W - 2, y + 8,
                                AlignRight, AlignBottom, fval);
        set_fg(canvas, app);
    }

    // Row: Dark mode
    {
        uint8_t row_offset = (app->dict_count > 1) ? 2 : 1;
        uint8_t y   = BODY_Y + ITEM_H * row_offset;
        bool    sel = (app->settings_sel == SettingsRowDark);
        if(sel) {
            set_fg(canvas, app);
            canvas_draw_box(canvas, 0, y, SCREEN_W, ITEM_H);
            set_bg(canvas, app);
        } else {
            set_fg(canvas, app);
        }
        canvas_draw_str(canvas, 3, y + 8, "Dark Mode");
        canvas_draw_str_aligned(canvas, SCREEN_W - 2, y + 8,
                                AlignRight, AlignBottom,
                                app->dark_mode ? "[On]" : "[Off]");
        set_fg(canvas, app);
    }

    set_fg(canvas, app);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, SCREEN_W / 2, SCREEN_H - 2,
                            AlignCenter, AlignBottom,
                            "OK=Save  Long-OK=List");
}

// ============================================================
// Scene: Favorites list
// ============================================================

static void draw_favorites(Canvas* canvas, App* app) {
    if(app->dark_mode) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, SCREEN_W, SCREEN_H);
    }

    char hdr_buf[24];
    if(app->fav_count == 0)
        snprintf(hdr_buf, sizeof(hdr_buf), "Favorites");
    else
        snprintf(hdr_buf, sizeof(hdr_buf), "Favorites (%d)", (int)app->fav_count);
    draw_hdr(canvas, hdr_buf);

    set_fg(canvas, app);
    canvas_set_font(canvas, FontSecondary);

    if(app->fav_count == 0) {
        canvas_draw_str_aligned(canvas, SCREEN_W / 2, 34,
                                AlignCenter, AlignCenter, "No favorites yet.");
        canvas_draw_str_aligned(canvas, SCREEN_W / 2, 46,
                                AlignCenter, AlignCenter, "Long-OK in entry view.");
        return;
    }

    const uint8_t LINE_H = 10;
    const uint8_t vis    = (uint8_t)((SCREEN_H - HDR_H - 2) / LINE_H);

    if(app->fav_sel < app->fav_scroll)
        app->fav_scroll = app->fav_sel;
    if(app->fav_sel >= app->fav_scroll + vis)
        app->fav_scroll = (uint8_t)(app->fav_sel - vis + 1);

    for(uint8_t i = 0; i < vis && (app->fav_scroll + i) < app->fav_count; i++) {
        uint8_t si  = app->fav_scroll + i;
        uint8_t y   = HDR_H + 2 + i * LINE_H;
        bool    sel = (si == app->fav_sel);

        if(sel) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, y - 1, SCREEN_W - SB_W - 2, LINE_H);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_set_color(canvas, ColorBlack);
        }

        char disp[24];
        truncate_utf8_display(app->favorites[si].word, disp, sizeof(disp), 18);
        set_ui_font(canvas, disp);
        canvas_draw_str(canvas, 4, y + 8, disp);
        canvas_set_font(canvas, FontSecondary);
        canvas_set_color(canvas, ColorBlack);
    }

    draw_scrollbar(canvas, app, app->fav_scroll, app->fav_count, vis);
}

// ============================================================
// Scene: About
// ============================================================

static const char* const ABOUT_LINES[] = {
    APP_NAME " v" APP_VERSION,
    "---------------------",
    "  dict.cc dictionary",
    "    viewer app.",
    "---------------------",
    "SD card setup:",
    "  /ext/apps_data/",
    "   fz_dict_app/",
    "  <DictName>/",
    "   a.txt  b.txt ...",
    "   z.txt  0.txt _.txt",
    "  (one letter-file",
    "   per first char)",
    "  Multiple folders",
    "  = multiple dicts.",
    "  Use prepare_dict.py",
    "  to split dict.cc",
    "  export into folders.",
    "---------------------",
    "CONTROLS",
    "- - - - Main Menu - -",
    "  Up/Down = move row",
    "  L/R on Search row",
    "  = cycle dictionary",
    "  OK = open",
    "  Long-OK = Settings",
    "  Back = exit app",
    "- - - - Search - - -",
    "  Type word, press",
    "  GO! to search dict",
    "  Searches only the",
    "  matching letter",
    "  bucket (fast!)",
    "  Long-L/R=cycle hint",
    "  Long-Up = fill hint",
    "  Back = backspace",
    "  Long-Back = Menu",
    "- - - - Results - - -",
    "  Up/Down = navigate",
    "  OK = open entry",
    "  Back = back to kbd",
    "- - - - Entry - - - -",
    "  Up/Down = scroll",
    "  Left = prev result",
    "  Right = next result",
    "  Long-OK = favorite*",
    "  Back = back",
    "- - - - Favorites - -",
    "  Up/Down = navigate",
    "  OK = open entry",
    "  Long-OK = remove",
    "  Back = close",
    "---------------------",
    " * star shown in hdr",
    " when entry is saved.",
    " ",
};
#define ABOUT_LINE_COUNT ((uint8_t)(sizeof(ABOUT_LINES)/sizeof(ABOUT_LINES[0])))

static void draw_about(Canvas* canvas, App* app) {
    if(app->dark_mode) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, SCREEN_W, SCREEN_H);
    }
    draw_hdr(canvas, "About");

    set_fg(canvas, app);
    canvas_set_font(canvas, FontSecondary);
    const uint8_t lh  = 10;
    const uint8_t vis = (uint8_t)((SCREEN_H - HDR_H - 2) / lh);

    for(uint8_t i = 0; i < vis; i++) {
        uint8_t li = app->about_scroll + i;
        if(li >= ABOUT_LINE_COUNT) break;
        canvas_draw_str(canvas, 2, HDR_H + 2 + i * lh + lh - 1, ABOUT_LINES[li]);
    }
    draw_scrollbar(canvas, app, app->about_scroll, ABOUT_LINE_COUNT, vis);
}

// ============================================================
// Scene: Error
// ============================================================

static void draw_error(Canvas* canvas, App* app) {
    if(app->dark_mode) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, SCREEN_W, SCREEN_H);
    }
    draw_hdr(canvas, "Error");
    set_fg(canvas, app);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, SCREEN_W / 2, SCREEN_H / 2,
                            AlignCenter, AlignCenter, app->error_msg);
    canvas_draw_str_aligned(canvas, SCREEN_W / 2, SCREEN_H - 4,
                            AlignCenter, AlignBottom, "Back = menu");
}

// ============================================================
// Scene: Loading
// ============================================================

static void draw_loading(Canvas* canvas, App* app) {
    if(app->dark_mode) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, SCREEN_W, SCREEN_H);
    }
    draw_hdr(canvas, "Searching...");
    set_fg(canvas, app);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, SCREEN_W / 2, SCREEN_H / 2,
                            AlignCenter, AlignCenter, "Please wait...");
}

// ============================================================
// Draw callback (called on every frame by the GUI)
// ============================================================

static void draw_cb(Canvas* canvas, void* ctx) {
    App* app = (App*)ctx;
    canvas_clear(canvas);

    switch(app->view) {
    case ViewMenu:          draw_menu(canvas, app);    break;
    case ViewSearch:        draw_search_input(canvas, app);   break;
    case ViewSearchResults: draw_search_results(canvas, app); break;
    case ViewEntry:         draw_entry(canvas, app);   break;
    case ViewSettings:      draw_settings(canvas, app); break;
    case ViewFavorites:     draw_favorites(canvas, app); break;
    case ViewAbout:         draw_about(canvas, app);   break;
    case ViewLoading:       draw_loading(canvas, app); break;
    case ViewError:         draw_error(canvas, app);   break;
    }
}

// ============================================================
// Input callback
// ============================================================

static void input_cb(InputEvent* ev, void* ctx) {
    App* app = (App*)ctx;
    furi_message_queue_put(app->queue, ev, FuriWaitForever);
}

// ============================================================
// Input: Menu
// ============================================================

static void on_menu(App* app, InputEvent* ev) {
    // Long-press OK -> Settings
    if(ev->type == InputTypeLong && ev->key == InputKeyOk) {
        app->settings_sel        = (app->dict_count > 1) ? SettingsRowDict : SettingsRowFont;
        app->settings_font_sel   = (uint8_t)app->font_choice;
        app->settings_font_open  = false;
        app->settings_dict_sel   = app->dict_idx;
        app->settings_dict_open  = false;
        app->settings_long_consumed = false;
        app->menu_long_consumed  = true;
        app->view = ViewSettings;
        return;
    }
    if(ev->type == InputTypeRelease && ev->key == InputKeyOk) {
        app->menu_long_consumed = false;
        return;
    }
    if(app->menu_long_consumed) return;

    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;

    switch(ev->key) {
    case InputKeyUp:
        if((uint8_t)app->sel_row > 0) app->sel_row = (MenuRow)(app->sel_row - 1);
        else app->sel_row = (MenuRow)(MENU_ROWS - 1);
        break;
    case InputKeyDown:
        if((uint8_t)app->sel_row < MENU_ROWS - 1) app->sel_row = (MenuRow)(app->sel_row + 1);
        else app->sel_row = (MenuRow)0;
        break;

    // Left/Right on Search row: cycle active dictionary
    case InputKeyLeft:
        if(app->sel_row == RowSearch && app->dict_count > 1) {
            app->dict_idx = (app->dict_idx > 0) ?
                app->dict_idx - 1 : app->dict_count - 1;
        }
        break;
    case InputKeyRight:
        if(app->sel_row == RowSearch && app->dict_count > 1) {
            app->dict_idx = (app->dict_idx < app->dict_count - 1) ?
                app->dict_idx + 1 : 0;
        }
        break;

    case InputKeyOk:
        switch(app->sel_row) {
        case RowSearch:
            if(app->dict_count == 0) {
                strncpy(app->error_msg, "No dictionary on SD!",
                        sizeof(app->error_msg) - 1);
                app->error_msg[sizeof(app->error_msg) - 1] = '\0';
                app->view = ViewError;
                break;
            }
            // Open search keyboard
            memset(app->search_buf, 0, sizeof(app->search_buf));
            app->search_len   = 0;
            app->kb_row       = 0;
            app->kb_col       = 0;
            app->kb_page      = 0;
            app->kb_caps      = false;
            app->hit_count    = 0;
            app->hit_sel      = 0;
            app->hit_scroll   = 0;
            app->suggest_count = 0;
            app->suggest_sel   = 0;
            app->suggest_long_consumed = false;
            app->kb_back_long_consumed = false;
            app->cursor_pos   = 0;
            app->text_scroll  = 0;
            app->cursor_blink = 0;
            app->bm_naming    = false;
            app->view = ViewSearch;
            break;
        case RowFavorites:
            app->fav_sel    = 0;
            app->fav_scroll = 0;
            app->view = ViewFavorites;
            break;
        case RowSettings:
            app->settings_sel        = (app->dict_count > 1) ? SettingsRowDict : SettingsRowFont;
            app->settings_font_sel   = (uint8_t)app->font_choice;
            app->settings_font_open  = false;
            app->settings_dict_sel   = app->dict_idx;
            app->settings_dict_open  = false;
            app->settings_long_consumed = false;
            app->view = ViewSettings;
            break;
        case RowAbout:
            app->about_scroll = 0;
            app->view = ViewAbout;
            break;
        default: break;
        }
        break;

    case InputKeyBack:
        app->running = false;
        break;

    default: break;
    }
}

// ============================================================
// Input: Entry (Reading) view
// ============================================================

static void on_entry(App* app, InputEvent* ev) {
    uint8_t vis = font_visible_lines(app->font_choice);

    // Long-press OK: toggle favorite
    if(ev->type == InputTypeLong && ev->key == InputKeyOk) {
        toggle_favorite(app);
        return;
    }

    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;

    switch(ev->key) {
    case InputKeyUp:
        if(app->wrap.scroll > 0) app->wrap.scroll--;
        break;
    case InputKeyDown:
        if(app->wrap.scroll + vis < app->wrap.count)
            app->wrap.scroll++;
        break;

    // Left/Right: navigate to prev/next search result
    case InputKeyLeft:
        if(app->hit_count > 1 && app->hit_sel > 0) {
            app->hit_sel--;
            app->view = ViewLoading;
            view_port_update(app->view_port);
            open_entry(app);
        }
        break;
    case InputKeyRight:
        if(app->hit_count > 1 && app->hit_sel < app->hit_count - 1) {
            app->hit_sel++;
            app->view = ViewLoading;
            view_port_update(app->view_port);
            open_entry(app);
        }
        break;

    case InputKeyOk:
        // OK in entry view: scroll down one page (same as Down)
        if(app->wrap.scroll + vis < app->wrap.count)
            app->wrap.scroll = (uint16_t)(app->wrap.scroll + vis);
        break;

    case InputKeyBack:
        app->view = app->prev_view;
        break;

    default: break;
    }
}

// ============================================================
// Input: Settings
// ============================================================

static void on_settings(App* app, InputEvent* ev) {
    // Long-press OK: open list for currently selected row
    if(ev->type == InputTypeLong && ev->key == InputKeyOk) {
        if(app->settings_long_consumed) return;
        if(app->settings_sel == SettingsRowFont && !app->settings_font_open) {
            app->settings_font_open = true;
            app->settings_font_sel  = (uint8_t)app->font_choice;
        } else if(app->settings_sel == SettingsRowDict && !app->settings_dict_open) {
            app->settings_dict_open = true;
            app->settings_dict_sel  = app->dict_idx;
        }
        app->settings_long_consumed = true;
        return;
    }
    if(ev->type == InputTypeRelease && ev->key == InputKeyOk) {
        app->settings_long_consumed = false;
        return;
    }
    if(app->settings_long_consumed) return;

    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;

    if(app->settings_font_open) {
        switch(ev->key) {
        case InputKeyUp:
            if(app->settings_font_sel > 0) app->settings_font_sel--;
            break;
        case InputKeyDown:
            if(app->settings_font_sel < FONT_COUNT - 1) app->settings_font_sel++;
            break;
        case InputKeyOk:
            app->font_choice       = (FontChoice)app->settings_font_sel;
            app->settings_font_open = false;
            break;
        case InputKeyBack:
            app->settings_font_open = false;
            break;
        default: break;
        }
        return;
    }

    if(app->settings_dict_open) {
        switch(ev->key) {
        case InputKeyUp:
            if(app->settings_dict_sel > 0) app->settings_dict_sel--;
            break;
        case InputKeyDown:
            if(app->settings_dict_sel < app->dict_count - 1) app->settings_dict_sel++;
            break;
        case InputKeyOk:
            app->dict_idx          = app->settings_dict_sel;
            app->settings_dict_open = false;
            break;
        case InputKeyBack:
            app->settings_dict_open = false;
            break;
        default: break;
        }
        return;
    }

    // Collapsed settings view — navigate only the rows that are actually visible.
    // When dict_count <= 1 the Dictionary row is hidden; valid rows are Font and Dark only.
    const uint8_t first_row = (app->dict_count > 1) ?
                              (uint8_t)SettingsRowDict : (uint8_t)SettingsRowFont;
    const uint8_t last_row  = (uint8_t)SettingsRowDark;

    switch(ev->key) {
    case InputKeyUp:
        if((uint8_t)app->settings_sel > first_row)
            app->settings_sel = (SettingsRow)((uint8_t)app->settings_sel - 1);
        else
            app->settings_sel = (SettingsRow)last_row;
        break;
    case InputKeyDown:
        if((uint8_t)app->settings_sel < last_row)
            app->settings_sel = (SettingsRow)((uint8_t)app->settings_sel + 1);
        else
            app->settings_sel = (SettingsRow)first_row;
        break;
    case InputKeyLeft:
    case InputKeyRight:
        switch(app->settings_sel) {
        case SettingsRowDark:
            app->dark_mode = !app->dark_mode;
            break;
        case SettingsRowFont:
            if(ev->key == InputKeyRight)
                app->font_choice = (FontChoice)((app->font_choice + 1) % FONT_COUNT);
            else
                app->font_choice = (FontChoice)((app->font_choice + FONT_COUNT - 1) % FONT_COUNT);
            break;
        case SettingsRowDict:
            if(app->dict_count > 1) {
                if(ev->key == InputKeyRight)
                    app->dict_idx = (app->dict_idx + 1) % app->dict_count;
                else
                    app->dict_idx = (app->dict_idx + app->dict_count - 1) % app->dict_count;
            }
            break;
        default: break;
        }
        break;
    case InputKeyOk:
        // Save and close
        settings_save(app);
        app->view = ViewMenu;
        break;
    case InputKeyBack:
        app->view = ViewMenu;
        break;
    default: break;
    }
}

// ============================================================
// Input: About
// ============================================================

static void on_about(App* app, InputEvent* ev) {
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    const uint8_t lh  = 10;
    const uint8_t vis = (uint8_t)((SCREEN_H - HDR_H - 2) / lh);
    switch(ev->key) {
    case InputKeyUp:
        if(app->about_scroll > 0) app->about_scroll--;
        break;
    case InputKeyDown:
        if(app->about_scroll + vis < ABOUT_LINE_COUNT) app->about_scroll++;
        break;
    case InputKeyOk:
    case InputKeyBack:
        app->view = ViewMenu;
        break;
    default: break;
    }
}

// ============================================================
// Input: Favorites list
// ============================================================

static void on_favorites(App* app, InputEvent* ev) {
    const uint8_t LINE_H = 10;
    const uint8_t vis    = (uint8_t)((SCREEN_H - HDR_H - 2) / LINE_H);

    // Long-press OK: remove from favorites
    if(ev->type == InputTypeLong && ev->key == InputKeyOk) {
        if(app->fav_count == 0) return;
        uint8_t fi = app->fav_sel;
        // Remove entry fi
        for(uint8_t i = fi; i + 1 < app->fav_count; i++)
            app->favorites[i] = app->favorites[i + 1];
        app->fav_count--;
        if(app->fav_sel >= app->fav_count && app->fav_count > 0)
            app->fav_sel = app->fav_count - 1;
        favorites_save(app);
        return;
    }

    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;

    switch(ev->key) {
    case InputKeyUp:
        if(app->fav_count == 0) break;
        if(app->fav_sel > 0) app->fav_sel--;
        else app->fav_sel = app->fav_count - 1;
        break;
    case InputKeyDown:
        if(app->fav_count == 0) break;
        if(app->fav_sel < app->fav_count - 1) app->fav_sel++;
        else app->fav_sel = 0;
        break;
    case InputKeyOk:
        if(app->fav_count == 0) break;
        open_entry_from_fav(app);
        break;
    case InputKeyBack:
        app->view = ViewMenu;
        break;
    default: break;
    }

    if(app->fav_count > 0) {
        if(app->fav_sel < app->fav_scroll)
            app->fav_scroll = app->fav_sel;
        if(app->fav_sel >= app->fav_scroll + vis)
            app->fav_scroll = (uint8_t)(app->fav_sel - vis + 1);
    }
}

// ============================================================
// Entry point
// ============================================================

int32_t fz_dict_app(void* p) {
    UNUSED(p);

    App* app = malloc(sizeof(App));
    if(!app) return -1;
    memset(app, 0, sizeof(App));

    app->running   = true;
    app->view      = ViewLoading;
    app->prev_view = ViewMenu;
    app->sel_row   = RowSearch;

    app->storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(app->storage, DATA_DIR);

    app->queue     = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_cb, app);
    view_port_input_callback_set(app->view_port, input_cb, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    view_port_update(app->view_port);

    // Scan SD for available dictionary files
    dicts_scan(app);

    // Load settings (restores dict_idx, font, dark mode)
    settings_load(app);

    // Clamp dict_idx in case dicts changed on card
    if(app->dict_idx >= app->dict_count && app->dict_count > 0)
        app->dict_idx = 0;

    // Load saved favorites
    favorites_load(app);

    // Load keyword suggestions (from keywords.txt on SD, if present)
    keywords_load(app);

    app->view = ViewMenu;
    view_port_update(app->view_port);

    InputEvent ev;
    while(app->running) {
        if(furi_message_queue_get(app->queue, &ev, 100) != FuriStatusOk)
            continue;

        switch(app->view) {
        case ViewMenu:          on_menu(app, &ev);           break;
        case ViewSearch:        on_search(app, &ev);         break;
        case ViewSearchResults: on_search_results(app, &ev); break;
        case ViewEntry:         on_entry(app, &ev);          break;
        case ViewSettings:      on_settings(app, &ev);       break;
        case ViewFavorites:     on_favorites(app, &ev);      break;
        case ViewAbout:         on_about(app, &ev);          break;
        case ViewError:
            if(ev.type == InputTypeShort && ev.key == InputKeyBack)
                app->view = ViewMenu;
            break;
        case ViewLoading: break;
        }

        view_port_update(app->view_port);
    }

    // Save settings on exit
    settings_save(app);

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(app->queue);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
