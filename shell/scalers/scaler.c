#include <stdio.h>
#include <stdint.h>
#include "scaler.h"

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

/* FAST_DOWNSCALER is for the Bittboy as it is a bit faster than the standard, nicer looking one. (It's 3x faster in fact) */
#ifdef FAST_DOWNSCALER

//from RGB565
#define cR(A) (((A) & 0xf800) >> 11)
#define cG(A) (((A) & 0x7e0) >> 5)
#define cB(A) ((A) & 0x1f)
//to RGB565
#define Weight1_1(A, B)  ((((cR(A) + cR(B)) >> 1) & 0x1f) << 11 | (((cG(A) + cG(B)) >> 1) & 0x3f) << 5 | (((cB(A) + cB(B)) >> 1) & 0x1f))
 
/* Downscales a 384x224 image to 320x224.
 *
 * Input:
 *   src: A packed 384x224 pixel image. The pixel format of this image is RGB 565.
 * Output:
 *   dst: A packed 320x224 pixel image. The pixel format of this image is RGB 565.
 */
 
void scale_384x224_to_320x224(uint32_t* dst, uint32_t* src)
{
    uint16_t* Src16 = (uint16_t*) src;
    uint16_t* Dst16 = (uint16_t*) dst;
 
    // There are 64 blocks of 6 pixels horizontally, and 224 of 1 vertically.
    // Each block of 6x1 becomes 5x1.
 
    uint16_t* BlockSrc;
    uint16_t* BlockDst;
    for (uint8_t BlockY = 0; BlockY < 224; BlockY++)
    {
        BlockSrc = Src16 + BlockY * 384 * 1;
        BlockDst = Dst16 + BlockY * 320 * 1;
        for (uint8_t BlockX = 0; BlockX < 64; BlockX++)
        {
            // HORIZONTAL: 6 --> 5
            // Before:                After:
            // (a)(b)(c)(d)(e)(f)    (a)(b)(c)(d)(ef)
 
            *(BlockDst    ) = *(BlockSrc    );
            *(BlockDst + 1) = *(BlockSrc + 1);
            *(BlockDst + 2) = *(BlockSrc + 2);
            *(BlockDst + 3) = *(BlockSrc + 3);
            *(BlockDst + 4) = Weight1_1(*(BlockSrc + 4), *(BlockSrc + 5));
 
            BlockSrc += 6;
            BlockDst += 5;
        }
    }
}

#else

/* This is the nicer one. Runs 3x slower than the one above according to gprof but does look smoother. */

//from RGB565
#define cR(A) (((A) & 0xf800) >> 11)
#define cG(A) (((A) & 0x7e0) >> 5)
#define cB(A) ((A) & 0x1f)
//to RGB565
#define Weight1_4(A, B)  ((((cR(A) + (cR(B) << 2)) / 5) & 0x1f) << 11 | (((cG(A) + (cG(B) << 2)) / 5) & 0x3f) << 5 | (((cB(A) + (cB(B) << 2)) / 5) & 0x1f))
#define Weight4_1(A, B)  ((((cR(B) + (cR(A) << 2)) / 5) & 0x1f) << 11 | (((cG(B) + (cG(A) << 2)) / 5) & 0x3f) << 5 | (((cB(B) + (cB(A) << 2)) / 5) & 0x1f))
#define Weight2_3(A, B)  (((((cR(A) << 1) + (cR(B) * 3)) / 5) & 0x1f) << 11 | ((((cG(A) << 1) + (cG(B) * 3)) / 5) & 0x3f) << 5 | ((((cB(A) << 1) + (cB(B) * 3)) / 5) & 0x1f))
#define Weight3_2(A, B)  (((((cR(B) << 1) + (cR(A) * 3)) / 5) & 0x1f) << 11 | ((((cG(B) << 1) + (cG(A) * 3)) / 5) & 0x3f) << 5 | ((((cB(B) << 1) + (cB(A) * 3)) / 5) & 0x1f))
 
/* Downscales a 384x224 image to 320x224.
 *
 * Input:
 *   src: A packed 384x224 pixel image. The pixel format of this image is RGB 565.
 * Output:
 *   dst: A packed 320x224 pixel image. The pixel format of this image is RGB 565.
 */
 
void scale_384x224_to_320x224(uint32_t* dst, uint32_t* src)
{
    uint16_t* Src16 = (uint16_t*) src;
    uint16_t* Dst16 = (uint16_t*) dst;
 
    // There are 64 blocks of 6 pixels horizontally, and 224 of 1 vertically.
    // Each block of 6x1 becomes 5x1.
 
    uint16_t* BlockSrc;
    uint16_t* BlockDst;
    for (uint8_t BlockY = 0; BlockY < 224; BlockY++)
    {
        BlockSrc = Src16 + BlockY * 384 * 1;
        BlockDst = Dst16 + BlockY * 320 * 1;
        for (uint8_t BlockX = 0; BlockX < 64; BlockX++)
        {
            // HORIZONTAL: 6 --> 5
            // Before:                After:
            // (a)(b)(c)(d)(e)(f)    (a)(bbbbc)(cccdd)(ddeee)(effff)
 
            uint16_t  _1 = *(BlockSrc    );
            *(BlockDst    ) = _1;
            _1 = *(BlockSrc + 1);
            uint16_t  _2 = *(BlockSrc + 2);
            *(BlockDst + 1) = Weight4_1( _1,  _2);
            _1 = *(BlockSrc + 3);
            *(BlockDst + 2) = Weight3_2( _2,  _1);
            _2 = *(BlockSrc + 4);
            *(BlockDst + 3) = Weight2_3( _1,  _2);
            _1 = *(BlockSrc + 5);
            *(BlockDst + 4) = Weight1_4( _2,  _1);
 
            BlockSrc += 6;
            BlockDst += 5;
        }
    }
}

#endif
