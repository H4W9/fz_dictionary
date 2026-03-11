#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <gui/gui.h>
#include <gui/canvas.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <storage/storage.h>

// ============================================================
// Constants
// ============================================================

#define SCREEN_W        128
#define SCREEN_H         64
#define DATA_DIR        "/ext/apps_data/fz_dict_app"
#define SETTINGS_PATH   DATA_DIR "/settings.txt"
#define FAVORITES_PATH  DATA_DIR "/favorites.txt"
#define KEYWORDS_PATH   DATA_DIR "/keywords.txt"
#define HISTORY_PATH    DATA_DIR "/history.txt"

#define MAX_DICTS           8
#define DICT_NAME_LEN       48
#define MAX_HISTORY         50
#define HISTORY_TERM_LEN    65   // MAX_SEARCH_LEN + 1
#define MAX_FAVORITES       50
#define FAV_WORD_LEN        48
#define WORD_DISPLAY_LEN    64   // source word display buffer
#define TRANS_DISPLAY_LEN   64   // translation preview buffer
#define HIT_REF_LEN         72   // formatted display string per hit
#define ENTRY_WORD_LEN      96   // full source word for entry view header
#define ENTRY_TEXT_LEN     512   // full translation text for entry view

#define HDR_H            12
#define MENU_ROW_H       10
#define MENU_ROWS         5
#define MENU_VIS          5
#define MENU_BODY_Y      (HDR_H + 1)
#define READ_BODY_Y      (HDR_H + 2)
#define WRAP_MAX_LINES  256

#define SB_W              3
#define SB_X             (SCREEN_W - SB_W - 1)

#define FONT_COUNT        5

#define KB_NROWS          3
#define KB_NCOLS         13
#define MAX_SEARCH_LEN   64
#define MAX_SEARCH_HITS 100

#define KEYWORDS_PATH_DICT  DATA_DIR "/keywords.txt"
#define MAX_KEYWORDS    200
#define KEYWORD_WORD_LEN 32
#define SUGGEST_MAX       5

#define LINE_BUF_LEN     512   // max bytes per dict file line
#define SEARCH_CHUNK_SIZE 512  // SD read chunk size for streaming search

// ============================================================
// Enums
// ============================================================

typedef enum {
    ViewMenu,
    ViewSearch,
    ViewSearchResults,
    ViewEntry,
    ViewSettings,
    ViewFavorites,
    ViewAbout,
    ViewHistory,
    ViewLoading,
    ViewError,
} AppView;

typedef enum {
    RowSearch    = 0,
    RowFavorites = 1,
    RowHistory   = 2,
    RowSettings  = 3,
    RowAbout     = 4,
} MenuRow;

typedef enum {
    FontSecondaryBuiltin = 0,
    FontSmall            = 1,
    FontMedium           = 2,
    FontLarge            = 3,
    FontXLarge           = 4,
} FontChoice;

typedef enum {
    SettingsRowDict  = 0,
    SettingsRowFont  = 1,
    SettingsRowDark  = 2,
    SettingsRowCount = 3,
} SettingsRow;

// ============================================================
// Structs
// ============================================================

typedef struct {
    char     lines[WRAP_MAX_LINES][40];
    uint16_t count;
    uint16_t scroll;
} WrapState;

typedef struct App {
    Gui*              gui;
    ViewPort*         view_port;
    FuriMessageQueue* queue;
    Storage*          storage;

    bool    running;
    AppView view;
    AppView prev_view;

    // Menu state
    MenuRow sel_row;
    uint8_t menu_scroll;
    bool    menu_long_consumed;

    // Display settings
    FontChoice font_choice;
    bool       dark_mode;

    // About
    uint8_t about_scroll;

    // Error
    char error_msg[64];

    // Search keyboard state
    char    search_buf[MAX_SEARCH_LEN + 1];
    uint8_t search_len;
    uint8_t kb_row;
    uint8_t kb_col;
    uint8_t kb_page;
    bool    kb_caps;
    bool    kb_long_consumed;
    bool    kb_back_long_consumed;
    uint8_t cursor_pos;
    uint8_t text_scroll;
    uint8_t cursor_blink;
    uint8_t list_tick;    // free-running frame counter for list row marquee scrolling
    bool    bm_naming;  // always false in dict app; kept for keyboard.c compatibility

    // Keyword suggestions
    uint16_t kw_count;
    char     suggest[SUGGEST_MAX][KEYWORD_WORD_LEN];
    uint8_t  suggest_count;
    uint8_t  suggest_sel;
    bool     suggest_long_consumed;

    // Search results
    struct {
        uint32_t file_offset;             // byte offset of line start in dict file
        char     word[WORD_DISPLAY_LEN];  // source word (display)
        char     trans[TRANS_DISPLAY_LEN];// translation preview
        char     ref[HIT_REF_LEN];        // formatted display line for results list
    } hits[MAX_SEARCH_HITS];
    uint8_t hit_count;
    uint8_t hit_sel;
    uint8_t hit_scroll;

    // Entry / reading view
    char      entry_word[ENTRY_WORD_LEN];  // full source word → used as header
    char      entry_text[ENTRY_TEXT_LEN];  // full translation text → wrapped body
    WrapState wrap;
    bool      entry_is_fav;                // true if current entry is in favorites

    // Dictionaries detected on SD card
    char    dicts[MAX_DICTS][DICT_NAME_LEN];
    uint8_t dict_count;
    uint8_t dict_idx;

    // Favorites (saved entries)
    struct {
        char     word[FAV_WORD_LEN];  // source word label
        uint32_t file_offset;         // byte offset for direct jump
        uint8_t  dict_idx;            // which dict file this entry is from
    } favorites[MAX_FAVORITES];
    uint8_t fav_count;
    uint8_t fav_sel;
    uint8_t fav_scroll;
    bool    fav_confirm_delete;  // true while waiting for confirm on remove-one
    bool    fav_long_consumed;

    // Search history
    char    history[MAX_HISTORY][HISTORY_TERM_LEN];
    uint8_t hist_count;
    uint8_t hist_sel;
    uint8_t hist_scroll;
    bool    hist_confirm_clear;   // true while waiting for confirm on clear-all
    bool    hist_confirm_delete;  // true while waiting for confirm on delete-one
    bool    hist_long_consumed;

    // Settings view state
    SettingsRow settings_sel;
    uint8_t     settings_font_sel;
    bool        settings_font_open;
    uint8_t     settings_dict_sel;
    bool        settings_dict_open;
    bool        settings_long_consumed;

} App;

// ============================================================
// Shared function declarations (defined in fz_dict.c,
// called by keyboard.c)
// ============================================================

void draw_hdr(Canvas* canvas, const char* title);
void draw_scrollbar(Canvas* canvas, App* app, uint16_t pos, uint16_t total, uint8_t vis);
void set_fg(Canvas* canvas, App* app);
void set_ui_font(Canvas* canvas, const char* str);
void do_search(App* app);
void history_add(App* app);
void history_load(App* app);
void open_entry(App* app);
void keywords_load(App* app);
void apply_font(Canvas* canvas, FontChoice f);
void truncate_utf8_display(const char* src, char* dst, size_t dst_size, uint8_t max_chars);
void suggestions_update(App* app);
void suggestion_fill(App* app);
