#ifndef FONT_DRAWING_H
#define FONT_DRAWING_H

#include <stdint.h>
#include <string.h>
#include "video_blit.h"

#if BPP_TOSET == 8
#define TextWhite 255
#define TextRed 128
#define TextBlue 4
#elif BPP_TOSET == 32
#define TextWhite SDL_MapRGB(backbuffer->format, 255, 255, 255)
#define TextRed SDL_MapRGB(backbuffer->format, 255, 0, 0)
#define TextBlue SDL_MapRGB(backbuffer->format, 0, 0, 255)
#else
#define TextWhite 65535
#define TextRed ((255>>3)<<11) + ((0>>2)<<5) + (0>>3)
#define TextBlue ((0>>3)<<11) + ((0>>2)<<5) + (255>>3)
#endif

void print_string(const char *s, const BPP_BITDEPTH fg_color, const BPP_BITDEPTH bg_color, uint32_t x, uint32_t y, BPP_BITDEPTH* buffer);

#endif
