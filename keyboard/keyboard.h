#pragma once
#include "../fz_dict.h"

extern const char kb_page0[KB_NROWS][KB_NCOLS];
extern const char kb_page1[KB_NROWS][KB_NCOLS];

const char* kb_key_label(App* app, uint8_t row, uint8_t col);
void draw_search_input(Canvas* canvas, App* app);
void draw_search_results(Canvas* canvas, App* app);
void on_search(App* app, InputEvent* ev);
void on_search_results(App* app, InputEvent* ev);
