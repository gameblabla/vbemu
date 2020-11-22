/* Cygne
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Dox dox@space.pl
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <SDL/SDL.h>
#include <sys/time.h>
#include <sys/types.h>

#include "video_blit.h"
#include "scaler.h"
#include "config.h"
#include "make_color.h"
#include "surface.h"

SDL_Surface *sdl_screen, *backbuffer;

#ifndef SDL_TRIPLEBUF
#define SDL_TRIPLEBUF SDL_DOUBLEBUF
#endif

/* GCW0 : Triple buffering causes flickering, for some reasons... */

#if defined(WANT_32BPP)
#define SDL_FLAGS SDL_HWSURFACE | SDL_DOUBLEBUF
uint32_t* __restrict__ internal_pix;
#elif defined(WANT_16BPP)
#define SDL_FLAGS SDL_HWSURFACE
uint16_t* __restrict__ internal_pix;
#elif defined(WANT_8BPP)
#define SDL_FLAGS SDL_HWSURFACE | SDL_HWPALETTE
uint8_t* __restrict__ internal_pix;
#endif

void Init_Video()
{
	setenv("SDL_VIDEO_REFRESHRATE", "50", 0);
	
	SDL_Init( SDL_INIT_VIDEO );

	SDL_ShowCursor(0);

	backbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 240, 16, 0,0,0,0);

	Set_Video_InGame();
}

void Set_Video_Menu()
{
	/* There's a memory leak if it gets init to 320x240...
	 * It could be an issue with the old SDL version that we are using..
	 * I don't know. We need to compare it against the GCW0's SDL.
	 * */
	sdl_screen = SDL_SetVideoMode(640, 480, 16, SDL_FLAGS);
	
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
}

void Set_Video_InGame()
{
#if defined(WANT_32BPP)
	sdl_screen = SDL_SetVideoMode(384, 224, 32, SDL_FLAGS);
	internal_pix = (uint32_t*)sdl_screen->pixels;
#elif defined(WANT_16BPP)
	sdl_screen = SDL_SetVideoMode(384, 224, 16, SDL_FLAGS);
	internal_pix = (uint16_t*)sdl_screen->pixels;
#elif defined(WANT_8BPP)
	sdl_screen = SDL_SetVideoMode(384, 224, 8, SDL_FLAGS);
	Set_VB_palette();
	internal_pix = (uint8_t*)sdl_screen->pixels;
#endif

	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
}

void Video_Close()
{
	if (sdl_screen)
	{
		SDL_FreeSurface(sdl_screen);
		sdl_screen = NULL;
	}
	if (backbuffer)
	{
		SDL_FreeSurface(backbuffer);
		backbuffer = NULL;
	}
	SDL_Quit();
}

void Update_Video_Menu()
{
	SDL_Flip(sdl_screen);
}

void Update_Video_Ingame(void)
{
	SDL_Flip(sdl_screen);
}
