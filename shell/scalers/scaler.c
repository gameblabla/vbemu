#include <stdio.h>
#include <stdint.h>
#include "scaler.h"

#define AVERAGE(z, x) ((((z) & 0xF7DEF7DE) >> 1) + (((x) & 0xF7DEF7DE) >> 1))
#define AVERAGEHI(AB) ((((AB) & 0xF7DE0000) >> 1) + (((AB) & 0xF7DE) << 15))
#define AVERAGELO(CD) ((((CD) & 0xF7DE) >> 1) + (((CD) & 0xF7DE0000) >> 17))

// Support math
#define Half(A) (((A) >> 1) & 0x7BEF)
#define Quarter(A) (((A) >> 2) & 0x39E7)
// Error correction expressions to piece back the lower bits together
#define RestHalf(A) ((A) & 0x0821)
#define RestQuarter(A) ((A) & 0x1863)

// Error correction expressions for quarters of pixels
#define Corr1_3(A, B)     Quarter(RestQuarter(A) + (RestHalf(B) << 1) + RestQuarter(B))
#define Corr3_1(A, B)     Quarter((RestHalf(A) << 1) + RestQuarter(A) + RestQuarter(B))

// Error correction expressions for halves
#define Corr1_1(A, B)     ((A) & (B) & 0x0821)

// Quarters
#define Weight1_3(A, B)   (Quarter(A) + Half(B) + Quarter(B) + Corr1_3(A, B))
#define Weight3_1(A, B)   (Half(A) + Quarter(A) + Quarter(B) + Corr3_1(A, B))

// Halves
#define Weight1_1(A, B)   (Half(A) + Half(B) + Corr1_1(A, B))


void upscale_256x240_to_320x240_bilinearish(uint32_t* restrict dst, uint32_t* restrict src, uint_fast16_t width, uint_fast16_t height)
{
	uint16_t* Src16 = (uint16_t*) src;
	uint16_t* Dst16 = (uint16_t*) dst;
	// There are 64 blocks of 4 pixels horizontally, and 239 of 1 vertically.
	// Each block of 4x1 becomes 5x1.
	uint32_t BlockX, BlockY;
	uint16_t* BlockSrc;
	uint16_t* BlockDst;
	for (BlockY = 0; BlockY < height; BlockY++)
	{
		BlockSrc = Src16 + BlockY * 512 * 1;
		BlockDst = Dst16 + BlockY * 320 * 1;
		for (BlockX = 0; BlockX < 64; BlockX++)
		{
			/* Horizontally:
			 * Before(4):
			 * (a)(b)(c)(d)
			 * After(5):
			 * (a)(abbb)(bc)(cccd)(d)
			 */

			// -- Row 1 --
			uint16_t  _1 = *(BlockSrc               );
			*(BlockDst               ) = _1;
			uint16_t  _2 = *(BlockSrc            + 1);
			*(BlockDst            + 1) = Weight1_3( _1,  _2);
			uint16_t  _3 = *(BlockSrc            + 2);
			*(BlockDst            + 2) = Weight1_1( _2,  _3);
			uint16_t  _4 = *(BlockSrc            + 3);
			*(BlockDst            + 3) = Weight3_1( _3,  _4);
			*(BlockDst            + 4) = _4;

			BlockSrc += 4;
			BlockDst += 5;
		}
	}
}

void upscale_256xXXX_to_320x240(uint32_t* restrict dst, uint32_t* restrict src, uint_fast16_t width, uint_fast16_t height)
{
    uint_fast16_t midh = 240;
    uint_fast16_t Eh = 0;
    uint_fast16_t source = 0;
    uint_fast16_t dh = 0;
    uint_fast8_t y, x;

    for (y = 0; y < 240; y++)
    {
        source = dh * width;

        for (x = 0; x < 320/10; x++)
        {
            register uint32_t ab, cd, ef, gh;

            __builtin_prefetch(dst + 4, 1);
            __builtin_prefetch(src + source + 4, 0);

            ab = src[source] & 0xF7DEF7DE;
            cd = src[source + 1] & 0xF7DEF7DE;
            ef = src[source + 2] & 0xF7DEF7DE;
            gh = src[source + 3] & 0xF7DEF7DE;

            if(Eh >= midh) 
			{
                ab = AVERAGE(ab, src[source + width/2]) & 0xF7DEF7DE; // to prevent overflow
                cd = AVERAGE(cd, src[source + width/2 + 1]) & 0xF7DEF7DE; // to prevent overflow
                ef = AVERAGE(ef, src[source + width/2 + 2]) & 0xF7DEF7DE; // to prevent overflow
                gh = AVERAGE(gh, src[source + width/2 + 3]) & 0xF7DEF7DE; // to prevent overflow
            }

            *dst++ = ab;
            *dst++  = ((ab >> 17) + ((cd & 0xFFFF) >> 1)) + (cd << 16);
            *dst++  = (cd >> 16) + (ef << 16);
            *dst++  = (ef >> 16) + (((ef & 0xFFFF0000) >> 1) + ((gh & 0xFFFF) << 15));
            *dst++  = gh;

            source += 4;

        }
        Eh += height; if(Eh >= 240) { Eh -= 240; dh++; }
    }
}


/* alekmaul's scaler taken from mame4all */
void bitmap_scale(uint32_t startx, uint32_t starty, uint32_t viswidth, uint32_t visheight, uint32_t newwidth, uint32_t newheight,uint32_t pitchsrc,uint32_t pitchdest, uint16_t* restrict src, uint16_t* restrict dst)
{
    uint32_t W,H,ix,iy,x,y;
    x=startx<<16;
    y=starty<<16;
    W=newwidth;
    H=newheight;
    ix=(viswidth<<16)/W;
    iy=(visheight<<16)/H;

    do 
    {
        uint16_t* restrict buffer_mem=&src[(y>>16)*pitchsrc];
        W=newwidth; x=startx<<16;
        do 
        {
            *dst++=buffer_mem[x>>16];
            x+=ix;
        } while (--W);
        dst+=pitchdest;
        y+=iy;
	} while (--H);
}


/* Downscales a 384x224 image to 320x224.
 *
 * Input:
 *   src: A packed 384x224 pixel image. The pixel format of this image is RGB 565.
 * Output:
 *   dst: A packed 320x224 pixel image. The pixel format of this image is RGB 565.
 */

#define cR(A) (((A) & 0xf800) >> 8)
#define cG(A) (((A) & 0x7e0) >> 3)
#define cB(A) (((A) & 0x1f) << 3)

#define Weight1_4(A, B)  ((((cR(A) + cR(B) + cR(B) + cR(B)+ cR(B)) / 5) & 0xf8) << 8 | (((cG(A) + cG(B) + cG(B) + cG(B) + cG(B)) / 5) & 0xfc) << 3 | (((cB(A) + cB(B) + cB(B) + cB(B) + cB(B)) / 5) & 0xf8) >> 3)
#define Weight4_1(A, B)  ((((cR(A) + cR(A) + cR(A) + cR(A)+ cR(B)) / 5) & 0xf8) << 8 | (((cG(A) + cG(A) + cG(A) + cG(A) + cG(B)) / 5) & 0xfc) << 3 | (((cB(A) + cB(A) + cB(A) + cB(A) + cB(B)) / 5) & 0xf8) >> 3)
#define Weight2_3(A, B)  ((((cR(A) + cR(A) + cR(B) + cR(B)+ cR(B)) / 5) & 0xf8) << 8 | (((cG(A) + cG(A) + cG(B) + cG(B) + cG(B)) / 5) & 0xfc) << 3 | (((cB(A) + cB(A) + cB(B) + cB(B) + cB(B)) / 5) & 0xf8) >> 3)
#define Weight3_2(A, B)  ((((cR(A) + cR(A) + cR(A) + cR(B)+ cR(B)) / 5) & 0xf8) << 8 | (((cG(A) + cG(A) + cG(A) + cG(B) + cG(B)) / 5) & 0xfc) << 3 | (((cB(A) + cB(A) + cB(A) + cB(B) + cB(B)) / 5) & 0xf8) >> 3)

void scale_384x224_to_320x224(uint32_t* restrict dst, uint32_t* restrict src)
{
    uint16_t* Src16 = (uint16_t*) src;
    uint16_t* Dst16 = (uint16_t*) dst;

    // There are 64 blocks of 6 pixels horizontally, and 224 of 1 vertically.
    // Each block of 6x1 becomes 5x1.

    uint32_t BlockX, BlockY;
    uint16_t* BlockSrc;
    uint16_t* BlockDst;
    for (BlockY = 0; BlockY < 224; BlockY++)
    {
        BlockSrc = Src16 + BlockY * 384;
        BlockDst = Dst16 + BlockY * 320;
        for (BlockX = 0; BlockX < 64; BlockX++)
        {
            // HORIZONTAL: 6 --> 5
            // Before:                After:
            // (a)(b)(c)(d)(e)(f)    (a)(bbbbc)(cccdd)(ddeee)(effff)

            uint16_t  _1 = *(BlockSrc    );
            *(BlockDst    ) = _1;
            uint16_t  _2 = *(BlockSrc + 1);
            uint16_t  _3 = *(BlockSrc + 2);
            *(BlockDst + 1) = Weight4_1( _2,  _3);
            uint16_t  _4 = *(BlockSrc + 3);
            *(BlockDst + 2) = Weight3_2( _3,  _4);
            uint16_t  _5 = *(BlockSrc + 4);
            *(BlockDst + 3) = Weight2_3( _4,  _5);
            uint16_t  _6 = *(BlockSrc + 5);
            *(BlockDst + 4) = Weight1_4( _5,  _6);

            BlockSrc += 6;
            BlockDst += 5;
        }
    }
}
