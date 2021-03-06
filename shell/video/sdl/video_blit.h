#ifndef VIDEO_BLIT_H
#define VIDEO_BLIT_H

#include <SDL/SDL.h>
#include <stdint.h>

#define HOST_WIDTH_RESOLUTION sdl_screen->w
#define HOST_HEIGHT_RESOLUTION sdl_screen->h

#define BACKBUFFER_WIDTH_RESOLUTION backbuffer->w
#define BACKBUFFER_HEIGHT_RESOLUTION backbuffer->h

extern SDL_Surface *sdl_screen, *backbuffer;

extern uint32_t width_of_surface;
#if defined(WANT_32BPP)
extern uint32_t* __restrict__ internal_pix;
#define BPP_TOSET 32
#define BPP_BITDEPTH uint32_t
#elif defined(WANT_16BPP)
extern uint16_t* __restrict__ internal_pix;
#define BPP_TOSET 16
#define BPP_BITDEPTH uint16_t
#elif defined(WANT_8BPP)
extern uint8_t* __restrict__ internal_pix;
#define BPP_TOSET 8
#define BPP_BITDEPTH uint8_t
#else
#error "Bitdepth not defined, SAD!"
#endif

void Init_Video();
void Set_Video_Menu();
void Set_Video_InGame();
void Video_Close();
void Update_Video_Menu();
void Update_Video_Ingame();


#endif
