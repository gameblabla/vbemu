#ifndef __MDFN_SURFACE_H
#define __MDFN_SURFACE_H

#include "../mednafen-types.h"

#if defined(WANT_32BPP)
#define RED_SHIFT 16
#define GREEN_SHIFT 8
#define BLUE_SHIFT 0
#define ALPHA_SHIFT 24
#define MAKECOLOR(r, g, b) ((r << RED_SHIFT) | (g << GREEN_SHIFT) | (b << BLUE_SHIFT) | (0 << ALPHA_SHIFT))
#elif defined(WANT_16BPP) && defined(FRONTEND_SUPPORTS_RGB565)
/* 16bit color - RGB565 */
#define RED_MASK  0xf800
#define GREEN_MASK 0x7e0
#define BLUE_MASK 0x1f
#define RED_EXPAND 3
#define GREEN_EXPAND 2
#define BLUE_EXPAND 3
#define RED_SHIFT 11
#define GREEN_SHIFT 5
#define BLUE_SHIFT 0
#define MAKECOLOR(r, g, b) (((r >> RED_EXPAND) << RED_SHIFT) | ((g >> GREEN_EXPAND) << GREEN_SHIFT) | ((b >> BLUE_EXPAND) << BLUE_SHIFT))
#elif defined(WANT_16BPP) && !defined(FRONTEND_SUPPORTS_RGB565)
/* 16bit color - RGB555 */
#define RED_MASK  0x7c00
#define GREEN_MASK 0x3e0
#define BLUE_MASK 0x1f
#define RED_EXPAND 3
#define GREEN_EXPAND 3
#define BLUE_EXPAND 3
#define RED_SHIFT 10
#define GREEN_SHIFT 5
#define BLUE_SHIFT 0
#define MAKECOLOR(r, g, b) (((r >> RED_EXPAND) << RED_SHIFT) | ((g >> GREEN_EXPAND) << GREEN_SHIFT) | ((b >> BLUE_EXPAND) << BLUE_SHIFT))
#elif defined(WANT_8BPP)
#include <SDL/SDL.h>
#include "make_color.h"
#define RED_MASK  0
#define GREEN_MASK 0
#define BLUE_MASK 0
#define RED_EXPAND 0
#define GREEN_EXPAND 0
#define BLUE_EXPAND 0
#define RED_SHIFT 0
#define GREEN_SHIFT 0
#define BLUE_SHIFT 0
#define MAKECOLOR(r, g, b) Make_Color(r, g, b)
#endif

typedef struct
{
 int32 x, y, w, h;
} MDFN_Rect;

enum
{
 MDFN_COLORSPACE_RGB = 0
};


#if defined(WANT_16BPP)
	#define WIDTH_TYPE uint16_t
#elif defined(WANT_8BPP)
	#define WIDTH_TYPE uint8_t
#elif defined(WANT_32BPP)
	#define WIDTH_TYPE uint32_t
#endif

struct MDFN_PixelFormat
{
 unsigned int bpp;
 unsigned int colorspace;

 uint8 Rshift;  // Bit position of the lowest bit of the red component
 uint8 Gshift;  // [...] green component
 uint8 Bshift;  // [...] blue component
 uint8 Ashift;  // [...] alpha component.
}; // MDFN_PixelFormat;

// Supports 32-bit RGBA
//  16-bit is WIP
struct MDFN_Surface //typedef struct
{
	WIDTH_TYPE *pixels;

   // w, h, and pitch32 should always be > 0
   int32 w;
   int32 h;

   union
   {
      int32 pitch32; // In pixels, not in bytes.
      int32 pitchinpix;	// New name, new code should use this.
   };

   struct MDFN_PixelFormat format;
};

#endif
