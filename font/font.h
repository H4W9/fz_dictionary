#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <gui/view.h>
#include <gui/elements.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef enum
    {
        FONT_SIZE_SMALL = 1,
        FONT_SIZE_MEDIUM = 2,
        FONT_SIZE_LARGE = 3,
        FONT_SIZE_XLARGE = 4
    } FontSize;

    // The custom font used as a fallback when a UI string contains umlauts.
    // FONT_SIZE_MEDIUM (u8g2_font_5x8_tf) is visually closest to FontSecondary.
    // Change this one constant if you need a different size later.
    #define UMLAUT_FALLBACK_FONT      FONT_SIZE_MEDIUM

    // Fallback for the header bar, which normally uses FontPrimary (~10px bold).
    // FONT_SIZE_LARGE (u8g2_font_6x10_tf) is the closest-sized custom font.
    // Change this one constant if you need a different size later.
    #define UMLAUT_FALLBACK_FONT_HDR  FONT_SIZE_LARGE

    extern bool canvas_set_font_custom(Canvas *canvas, FontSize font_size);
    extern void canvas_draw_str_multi(Canvas *canvas, uint8_t x, uint8_t y, const char *str);

    // Returns true if str contains any UTF-8 umlaut or German special character
    // (ä ö ü Ä Ö Ü ß). Use this to decide whether to fall back to the custom font.
    extern bool str_has_umlaut(const char* str);

#ifdef __cplusplus
}
#endif
