// keyboard.c -- Search keyboard & results for FZ Dictionary

#include "keyboard.h"
#include "../font/font.h"
#include <gui/elements.h>
#include <string.h>
#include <stdio.h>

// Keyboard page tables

const char kb_page0[KB_NROWS][KB_NCOLS] = {
    { 'q','w','e','r','t','y','u','i','o','p','7','8','9' },
    { 'a','s','d','f','g','h','j','k','l',':','4','5','6' },
    { 'z','x','c','v','b','n','m',',','.','0','1','2','3' },
};

const char kb_page1[KB_NROWS][KB_NCOLS] = {
    { '!','@','#','$','%','^','&','*','(',')','{','}','[' },
    { ']','<','>','?','/',';',':','\'','"','~','`','\\','|' },
    { '+','=','_',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ' },
};

// Page 2: umlauts and special characters
typedef struct { const char* label; } SpecialKey;
static const SpecialKey kb_page2[KB_NROWS][KB_NCOLS] = {
    { {"\xC3\xB1"},{"\xC3\xA7"},{"\xC3\xBF"},{"\xC3\xB8"},{"\xC3\xA5"},
      {"\xC3\xA6"},{"\xC3\x90"},{"\xC3\xBE"},{"\xC2\xA3"},{"\xC2\xA5"},
      {"\xC2\xA9"},{"\xC2\xAE"},{"\xC2\xB0"} },
    { {"\xE2\x80\x9C"},{"\xE2\x80\x9D"},{"\xE2\x80\x93"},{"\xE2\x80\x94"},
      {"\xC3\xA9"},{"\xC3\xA8"},{"\xC3\xAA"},{"\xC3\xAB"},{"\xC3\xAF"},
      {"\xC3\xAE"},{"\xC3\xA0"},{"\xC3\xA2"},{"\xC3\xB5"} },
    { {"\xC3\x84"},{"\xC3\xA4"},{"\xC3\x96"},{"\xC3\xB6"},{"\xC3\x9C"},{"\xC3\xBC"},
      {"\xC3\x9F"},{"\xC2\xA1"},{"\xC2\xBF"},{"\xC2\xAB"},{"\xC2\xBB"},
      {"\xE2\x80\x98"},{"\xE2\x80\x99"} },
};

// Maps each of the 13 keyboard columns to the nearest special button (0-4)
static const uint8_t col_to_btn[KB_NCOLS] = { 0,0,0, 1,1, 2,2,2, 3,3,3, 4,4 };
// Maps each special button back to a representative keyboard column
static const uint8_t btn_to_col[5]        = { 1, 3, 6, 9, 11 };

const char* kb_key_label(App* app, uint8_t row, uint8_t col) {
    static char buf[4];
    if(app->kb_page == 0) {
        char ch = kb_page0[row][col];
        if(app->kb_caps && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        buf[0] = ch; buf[1] = '\0'; return buf;
    }
    if(app->kb_page == 1) { buf[0] = kb_page1[row][col]; buf[1] = '\0'; return buf; }
    return kb_page2[row][col].label;
}

// UTF-8 safe backspace at cursor position
static void search_buf_backspace(App* app) {
    if(!app->cursor_pos) return;
    uint8_t cp = app->cursor_pos;
    while(cp > 0 && (app->search_buf[cp - 1] & 0xC0) == 0x80) cp--;
    if(cp > 0) cp--;
    uint8_t char_start = cp;
    uint8_t removed    = app->cursor_pos - char_start;
    memmove(app->search_buf + char_start,
            app->search_buf + app->cursor_pos,
            app->search_len - app->cursor_pos + 1);
    app->search_len  -= removed;
    app->cursor_pos   = char_start;
    app->search_buf[app->search_len] = '\0';
}

// Insert bytes at cursor
static void search_buf_insert(App* app, const char* s, uint8_t slen) {
    if(app->search_len + slen >= MAX_SEARCH_LEN) return;
    memmove(app->search_buf + app->cursor_pos + slen,
            app->search_buf + app->cursor_pos,
            app->search_len - app->cursor_pos + 1);
    memcpy(app->search_buf + app->cursor_pos, s, slen);
    app->search_len  += slen;
    app->cursor_pos  += slen;
    app->search_buf[app->search_len] = '\0';
}

// Field geometry for the search input box
#define FIELD_CHAR_W 5
#define FIELD_X      4
#define FIELD_W      (SCREEN_W - 8)
#define FIELD_CHARS  (FIELD_W / FIELD_CHAR_W)

static uint8_t byte_to_char(const char* buf, uint8_t byte_pos) {
    uint8_t chars = 0;
    for(uint8_t i = 0; i < byte_pos; i++)
        if(((uint8_t)buf[i] & 0xC0) != 0x80) chars++;
    return chars;
}

#define FIELD_MARGIN 4
static void update_text_scroll(App* app) {
    uint8_t cursor_char = byte_to_char(app->search_buf, app->cursor_pos);
    if(cursor_char >= app->text_scroll + FIELD_CHARS - FIELD_MARGIN)
        app->text_scroll = (uint8_t)(cursor_char - FIELD_CHARS + FIELD_MARGIN + 1);
    if(cursor_char < app->text_scroll + FIELD_MARGIN) {
        app->text_scroll = (cursor_char >= FIELD_MARGIN) ?
                           (uint8_t)(cursor_char - FIELD_MARGIN) : 0;
    }
}

// draw_search_input

void draw_search_input(Canvas* canvas, App* app) {
    // ── Header: suggestion dominates, fallback to page title ─────────────────
    static const char* const ptitles[] = { "Search", "Search: Sym", "Search: Uml" };
    const char* base_title = ptitles[app->kb_page < 3 ? app->kb_page : 0];
    if(app->suggest_count > 0) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, SCREEN_W, HDR_H);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font_custom(canvas, FONT_SIZE_MEDIUM);
        if(app->suggest_count > 1 && app->suggest_sel > 0)
            canvas_draw_str_aligned(canvas, 2, HDR_H / 2,
                                    AlignLeft, AlignCenter, "<");
        if(app->suggest_count > 1 && app->suggest_sel < app->suggest_count - 1)
            canvas_draw_str_aligned(canvas, SCREEN_W - 2, HDR_H / 2,
                                    AlignRight, AlignCenter, ">");
        char sug_hdr[KEYWORD_WORD_LEN + 8];
        if(app->suggest_count > 1)
            snprintf(sug_hdr, sizeof(sug_hdr), "%d/%d %s",
                     (int)(app->suggest_sel + 1), (int)app->suggest_count,
                     app->suggest[app->suggest_sel]);
        else
            snprintf(sug_hdr, sizeof(sug_hdr), "%s", app->suggest[app->suggest_sel]);
        canvas_draw_str_aligned(canvas, SCREEN_W / 2, HDR_H / 2,
                                AlignCenter, AlignCenter, sug_hdr);
        canvas_set_color(canvas, ColorBlack);
    } else {
        draw_hdr(canvas, base_title);
    }

    app->cursor_blink++;

    // ── Input field ───────────────────────────────────────────────────────
    canvas_set_font_custom(canvas, FONT_SIZE_MEDIUM);
    canvas_draw_frame(canvas, 2, HDR_H + 1, SCREEN_W - 4, 12);

    update_text_scroll(app);

    uint8_t bi = 0, col = 0;
    while(bi < app->search_len && col < app->text_scroll) {
        if(((uint8_t)app->search_buf[bi] & 0xC0) != 0x80) col++;
        bi++;
    }

    char disp[FIELD_CHARS + 8];
    uint8_t di = 0, chars_drawn = 0, byte_idx = bi;
    while(byte_idx < app->search_len &&
          chars_drawn < FIELD_CHARS + 1 &&
          di < (uint8_t)sizeof(disp) - 4) {
        uint8_t b = (uint8_t)app->search_buf[byte_idx];
        disp[di++] = app->search_buf[byte_idx++];
        while(byte_idx < app->search_len &&
              ((uint8_t)app->search_buf[byte_idx] & 0xC0) == 0x80 &&
              di < (uint8_t)sizeof(disp) - 1)
            disp[di++] = app->search_buf[byte_idx++];
        if((b & 0xC0) != 0x80) chars_drawn++;
    }
    disp[di] = '\0';

    canvas_draw_str(canvas, FIELD_X + 1, HDR_H + 10, disp);

    // ── Blinking cursor ─────────────────────────────────────────────────
    if((app->cursor_blink & 0x10) == 0) {
        uint8_t cursor_char = byte_to_char(app->search_buf, app->cursor_pos);
        uint8_t cx_chars = (cursor_char >= app->text_scroll) ?
                           (cursor_char - app->text_scroll) : 0;
        uint8_t cx = FIELD_X + 1 + cx_chars * FIELD_CHAR_W;
        if(cx <= FIELD_X + FIELD_W - 1) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_line(canvas, cx, HDR_H + 2, cx, HDR_H + 11);
        }
    }

    // ── Keyboard grid ─────────────────────────────────────────────────────
    const uint8_t bkh = 8;
    const uint8_t by  = SCREEN_H - bkh;
    const uint8_t kw  = 9;
    const uint8_t kh  = 10;
    const uint8_t ky  = by - KB_NROWS * kh;
    for(uint8_t r = 0; r < KB_NROWS; r++) {
        for(uint8_t c = 0; c < KB_NCOLS; c++) {
            uint8_t x = 4 + c * kw, y = ky + r * kh;
            bool sel = (r == app->kb_row && c == app->kb_col);
            if(sel) {
                canvas_set_color(canvas, ColorBlack);
                canvas_draw_box(canvas, x, y, kw - 1, kh - 1);
                canvas_set_color(canvas, ColorWhite);
            } else {
                canvas_set_color(canvas, ColorBlack);
            }
            canvas_draw_str(canvas, x + 1, y + 7, kb_key_label(app, r, c));
            canvas_set_color(canvas, ColorBlack);
        }
    }

    // ── Special buttons row ──────────────────────────────────────────────
    const char* btns[5] = {
        "DEL", "SPC",
        (app->kb_page == 0) ? "CAP" : "---",
        (app->kb_page == 0) ? "SYM" : (app->kb_page == 1) ? "UML" : "ABC",
        "GO!"
    };
    const uint8_t bx[5] = {  2, 27, 52, 77, 102 };
    const uint8_t bw[5] = { 23, 23, 23, 23,  23 };
    for(uint8_t i = 0; i < 5; i++) {
        bool btn_sel  = (app->kb_row == KB_NROWS && app->kb_col == i);
        bool caps_lit = (i == 2 && app->kb_page == 0 && app->kb_caps);
        bool fill = btn_sel || caps_lit;
        if(fill) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, bx[i], by, bw[i], bkh);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_frame(canvas, bx[i], by, bw[i], bkh);
        }
        canvas_set_font_custom(canvas, FONT_SIZE_SMALL);
        canvas_draw_str_aligned(canvas, bx[i] + bw[i] / 2, by + bkh - 1,
                                AlignCenter, AlignBottom, btns[i]);
        canvas_set_font(canvas, FontSecondary);
        canvas_set_color(canvas, ColorBlack);
    }

    // ── Highlight input box border when text field is focused ─────────────
    bool in_text_row = (app->kb_row == KB_NROWS + 1);
    if(in_text_row) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 1, HDR_H, SCREEN_W - 2, 14);
    }
}

// draw_search_results

void draw_search_results(Canvas* canvas, App* app) {
    char hdr_buf[32];
    if(app->hit_count == 0)
        snprintf(hdr_buf, sizeof(hdr_buf), "Not found");
    else
        snprintf(hdr_buf, sizeof(hdr_buf), "Found: %d", (int)app->hit_count);
    draw_hdr(canvas, hdr_buf);

    canvas_set_font(canvas, FontSecondary);

    if(app->hit_count == 0) {
        canvas_draw_str_aligned(canvas, SCREEN_W / 2, 34,
                                AlignCenter, AlignCenter, "No matches found");
        canvas_draw_str_aligned(canvas, SCREEN_W / 2, 46,
                                AlignCenter, AlignCenter, "Try different word");
        return;
    }

    app->list_tick++;

    const uint8_t LINE_H  = 10;
    const uint8_t MAX_VIS = 22;   // FONT_SIZE_MEDIUM 5x8 fixed: 22*5=110px < 118px available
    const uint8_t vis     = (uint8_t)((SCREEN_H - HDR_H - 2) / LINE_H);

    if(app->hit_sel < app->hit_scroll)
        app->hit_scroll = app->hit_sel;
    if(app->hit_sel >= app->hit_scroll + vis)
        app->hit_scroll = (uint8_t)(app->hit_sel - vis + 1);

    for(uint8_t i = 0; i < vis && (app->hit_scroll + i) < app->hit_count; i++) {
        uint8_t si  = app->hit_scroll + i;
        uint8_t y   = HDR_H + 2 + i * LINE_H;
        bool    sel = (si == app->hit_sel);

        if(sel) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, y - 1, SCREEN_W - SB_W - 2, LINE_H);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_set_color(canvas, ColorBlack);
        }

        char disp[HIT_REF_LEN];
        if(sel) {
            uint8_t cc = utf8_char_count(app->hits[si].ref);
            str_marquee_sub(app->hits[si].ref, cc, MAX_VIS, app->list_tick, app->scroll_speed, disp, sizeof(disp));
        } else {
            truncate_utf8_display(app->hits[si].ref, disp, sizeof(disp), MAX_VIS);
        }
        canvas_set_font_custom(canvas, FONT_SIZE_MEDIUM);
        canvas_draw_str(canvas, 4, y + 8, disp);
        canvas_set_color(canvas, ColorBlack);
    }

    draw_scrollbar(canvas, app, app->hit_scroll, app->hit_count, vis);
}

// on_search  (input handler for the keyboard view)

void on_search(App* app, InputEvent* ev) {

    // Long Back: always exit to previous view
    if(ev->type == InputTypeLong && ev->key == InputKeyBack) {
        app->view = app->prev_view;
        app->kb_back_long_consumed = true;
        return;
    }
    if(ev->type == InputTypeRelease && ev->key == InputKeyBack) {
        app->kb_back_long_consumed = false;
        return;
    }

    // Long Up: fill current suggestion + trailing space
    if(ev->type == InputTypeLong && ev->key == InputKeyUp) {
        if(app->suggest_count > 0) {
            suggestion_fill(app);
            if(app->search_len < MAX_SEARCH_LEN - 1)
                search_buf_insert(app, " ", 1);
            suggestions_update(app);
            app->suggest_long_consumed = true;
        }
        return;
    }
    if(ev->type == InputTypeRelease && ev->key == InputKeyUp) {
        app->suggest_long_consumed = false;
        return;
    }
    if(ev->type == InputTypeRepeat && ev->key == InputKeyUp && app->suggest_long_consumed) return;
    if(ev->type == InputTypeRelease && ev->key == InputKeyDown) {
        app->suggest_long_consumed = false;
        return;
    }
    if(ev->type == InputTypeRepeat && ev->key == InputKeyDown && app->suggest_long_consumed) return;

    // Long Down: insert a space at cursor
    if(ev->type == InputTypeLong && ev->key == InputKeyDown) {
        if(app->search_len < MAX_SEARCH_LEN - 1) {
            search_buf_insert(app, " ", 1);
            suggestions_update(app);
        }
        app->suggest_long_consumed = true;
        return;
    }

    // Long Left/Right: cycle through suggestions
    if(ev->type == InputTypeLong &&
       (ev->key == InputKeyLeft || ev->key == InputKeyRight)) {
        if(app->suggest_count > 1) {
            if(ev->key == InputKeyRight)
                app->suggest_sel = (app->suggest_sel + 1) % app->suggest_count;
            else
                app->suggest_sel = (app->suggest_sel > 0) ?
                                   app->suggest_sel - 1 : app->suggest_count - 1;
            app->suggest_long_consumed = true;
        }
        return;
    }
    if(ev->type == InputTypeRelease &&
       (ev->key == InputKeyLeft || ev->key == InputKeyRight)) {
        app->suggest_long_consumed = false;
        return;
    }
    if(ev->type == InputTypeRepeat &&
       (ev->key == InputKeyLeft || ev->key == InputKeyRight) &&
       app->suggest_long_consumed) return;

    // Hold OK: one-shot opposite-case letter
    if(ev->type == InputTypeLong && ev->key == InputKeyOk) {
        if(app->kb_row < KB_NROWS && app->kb_page == 0) {
            char ch = kb_page0[app->kb_row][app->kb_col];
            if(ch >= 'a' && ch <= 'z') {
                if(!app->kb_caps) ch = (char)(ch - 32);
                char s[2] = { ch, '\0' };
                search_buf_insert(app, s, 1);
                suggestions_update(app);
            }
        }
        app->kb_long_consumed = true;
        return;
    }
    if(ev->type == InputTypeRelease && ev->key == InputKeyOk) {
        app->kb_long_consumed = false;
        return;
    }
    if(ev->type == InputTypeRepeat && ev->key == InputKeyOk && app->kb_long_consumed) return;

    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;

    bool in_text_row = (app->kb_row == KB_NROWS + 1);

    switch(ev->key) {
    // ── Navigation ──────────────────────────────────────────────────────────
    case InputKeyUp:
        if(in_text_row) {
            app->kb_row = 0;
        } else if(app->kb_row == KB_NROWS) {
            app->kb_row = KB_NROWS - 1;
            app->kb_col = btn_to_col[app->kb_col];
        } else if(app->kb_row > 0) {
            app->kb_row--;
        } else {
            app->kb_row = KB_NROWS + 1;
        }
        break;

    case InputKeyDown:
        if(in_text_row) {
            app->kb_row = 0;
        } else if(app->kb_row < KB_NROWS - 1) {
            app->kb_row++;
        } else if(app->kb_row == KB_NROWS - 1) {
            app->kb_row = KB_NROWS;
            app->kb_col = col_to_btn[app->kb_col];
        } else {
            app->kb_row = KB_NROWS + 1;
        }
        break;

    case InputKeyLeft:
        if(in_text_row) {
            if(app->cursor_pos > 0) {
                app->cursor_pos--;
                while(app->cursor_pos > 0 &&
                      (app->search_buf[app->cursor_pos] & 0xC0) == 0x80)
                    app->cursor_pos--;
                update_text_scroll(app);
            }
        } else if(app->kb_row == KB_NROWS) {
            app->kb_col = (app->kb_col == 0) ? 4 : app->kb_col - 1;
        } else {
            app->kb_col = (app->kb_col == 0) ? KB_NCOLS - 1 : app->kb_col - 1;
        }
        break;

    case InputKeyRight:
        if(in_text_row) {
            if(app->cursor_pos < app->search_len) {
                app->cursor_pos++;
                while(app->cursor_pos < app->search_len &&
                      (app->search_buf[app->cursor_pos] & 0xC0) == 0x80)
                    app->cursor_pos++;
                update_text_scroll(app);
            }
        } else if(app->kb_row == KB_NROWS) {
            app->kb_col = (app->kb_col == 4) ? 0 : app->kb_col + 1;
        } else {
            app->kb_col = (app->kb_col == KB_NCOLS - 1) ? 0 : app->kb_col + 1;
        }
        break;

    // ── OK: type or activate special button ─────────────────────────────────
    case InputKeyOk:
        if(in_text_row) {
            app->kb_row = 0;
            break;
        }
        if(app->kb_row < KB_NROWS) {
            const char* s = kb_key_label(app, app->kb_row, app->kb_col);
            if(s && s[0]) {
                uint8_t slen = (uint8_t)strlen(s);
                search_buf_insert(app, s, slen);
                suggestions_update(app);
            }
        } else {
            switch(app->kb_col) {
            case 0: // DEL
                search_buf_backspace(app);
                suggestions_update(app);
                break;
            case 1: // SPC
                search_buf_insert(app, " ", 1);
                suggestions_update(app);
                break;
            case 2: // CAP
                if(app->kb_page == 0) app->kb_caps = !app->kb_caps;
                break;
            case 3: // SYM / UML / ABC
                app->kb_page = (app->kb_page == 0) ? 1 : (app->kb_page == 1) ? 2 : 0;
                break;
            case 4: // GO!
                if(app->search_len > 0) {
                    app->view = ViewLoading;
                    view_port_update(app->view_port);
                    do_search(app);
                    history_add(app);
                    // If Back was pressed during the blocking search, honour it:
                    // skip the results screen and return to where we came from.
                    InputEvent pending;
                    bool back_during_load =
                        (furi_message_queue_get(app->queue, &pending, 0) == FuriStatusOk) &&
                        (pending.key == InputKeyBack);
                    if(back_during_load) {
                        app->view = (app->search_origin == ViewHistory)
                                    ? ViewHistory : ViewMenu;
                    } else {
                        app->view = ViewSearchResults;
                    }
                }
                break;
            }
        }
        break;

    // ── Back: backspace at cursor, or exit ───────────────────────────────────
    case InputKeyBack:
        if(app->kb_back_long_consumed) break;
        // When launched from History the buffer is pre-filled; exit immediately
        // rather than forcing the user to backspace through the whole term.
        if(app->search_len > 0 && app->search_origin != ViewHistory) {
            search_buf_backspace(app);
            suggestions_update(app);
        } else {
            app->view = app->prev_view;
        }
        break;

    default: break;
    }
}

// on_search_results  (input handler for the results view)

void on_search_results(App* app, InputEvent* ev) {
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;

    const uint8_t LINE_H = 10;
    const uint8_t vis    = (uint8_t)((SCREEN_H - HDR_H - 2) / LINE_H);

    switch(ev->key) {
    case InputKeyUp:
        if(app->hit_sel > 0) app->hit_sel--;
        else app->hit_sel = app->hit_count - 1;
        break;
    case InputKeyDown:
        if(app->hit_sel < app->hit_count - 1) app->hit_sel++;
        else app->hit_sel = 0;
        break;
    case InputKeyOk:
        if(app->hit_count == 0) break;
        open_entry(app);
        break;
    case InputKeyBack:
        // Return to wherever search was originally launched from.
        // Using search_origin rather than prev_view because open_entry
        // overwrites prev_view with ViewSearchResults.
        if(app->search_origin == ViewHistory)
            app->view = ViewHistory;
        else
            app->view = ViewSearch;
        break;
    default: break;
    }

    if(app->hit_count > 0) {
        if(app->hit_sel < app->hit_scroll)
            app->hit_scroll = app->hit_sel;
        if(app->hit_sel >= app->hit_scroll + vis)
            app->hit_scroll = (uint8_t)(app->hit_sel - vis + 1);
    }
}
