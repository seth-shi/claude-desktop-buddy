#pragma once
#include <stdint.h>
// Light theme (RGB565), ported from the Rust theme.rs. Panel is BGR+inverted;
// these are written as normal RGB and the driver handles the transform.
#define RGB565(r, g, b) ((uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)))

static const uint16_t BG          = RGB565(0xF4, 0xF6, 0xF9); // near-white page (= sprite bg)
static const uint16_t INK         = RGB565(0x1B, 0x24, 0x33); // primary text
static const uint16_t DIM         = RGB565(0x62, 0x6E, 0x80); // secondary text
static const uint16_t CARD        = RGB565(0xFF, 0xFF, 0xFF); // info card fill
static const uint16_t CARD_STROKE = RGB565(0xDD, 0xE3, 0xEC); // card border
static const uint16_t TRACK       = RGB565(0xE5, 0xE9, 0xF1); // quota-bar track
static const uint16_t RED         = RGB565(0xDD, 0x4B, 0x39); // low / deny
static const uint16_t GOLD        = RGB565(0xCB, 0x82, 0x12); // amber accent
static const uint16_t GREEN       = RGB565(0x22, 0x9C, 0x53); // connected / ok
static const uint16_t CORAL       = RGB565(0xF0, 0x99, 0x7B); // heart accent
static const uint16_t TIMEBAR     = RGB565(0x3E, 0x86, 0xC8); // (unused) old blue time bar
static const uint16_t DRAGON_GREEN = RGB565(0x2A, 0x9C, 0x57); // shenron body green
static const uint16_t DRAGON_GOLD  = RGB565(0xF2, 0xA7, 0x36); // shenron belly gold
static const uint16_t COIN_EDGE    = RGB565(0xA8, 0x69, 0x12); // coin outline
