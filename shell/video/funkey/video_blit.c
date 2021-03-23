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

SDL_Surface *sdl_screen, *backbuffer, *vb_surface;

#ifndef SDL_TRIPLEBUF
#define SDL_TRIPLEBUF SDL_DOUBLEBUF
#endif

/* GCW0 : Triple buffering causes flickering, for some reasons... */
#if defined(WANT_32BPP)
#define SDL_FLAGS SDL_HWSURFACE | SDL_DOUBLEBUF
uint32_t* __restrict__ internal_pix;
#elif defined(WANT_16BPP)
#define SDL_FLAGS SDL_HWSURFACE | SDL_TRIPLEBUF
uint16_t* __restrict__ internal_pix;
#elif defined(WANT_8BPP)
#define SDL_FLAGS SDL_HWSURFACE | SDL_HWPALETTE
uint8_t* __restrict__ internal_pix;
#endif

#ifdef ENABLE_JOYSTICKCODE
SDL_Joystick* sdl_joy;
#endif

void Init_Video()
{
	setenv("SDL_VIDEO_REFRESHRATE", "50", 0);
	
	#ifdef ENABLE_JOYSTICKCODE
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
	
	if (SDL_NumJoysticks() > 0)
	{
		sdl_joy = SDL_JoystickOpen(0);
		SDL_JoystickEventState(SDL_ENABLE);
	}
	#else
	SDL_Init(SDL_INIT_VIDEO);
	#endif

	SDL_ShowCursor(0);

	backbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 240, BPP_TOSET, 0,0,0,0);
	vb_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, 384, 224, BPP_TOSET, 0,0,0,0);

	Set_Video_InGame();
}

void Set_Video_Menu()
{
	/* There's a memory leak if it gets init to 320x240...
	 * It could be an issue with the old SDL version that we are using..
	 * I don't know. We need to compare it against the GCW0's SDL.
	 * */
	sdl_screen = SDL_SetVideoMode(240, 240, BPP_TOSET, SDL_FLAGS);
	
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
}

void Set_Video_InGame()
{
	sdl_screen = SDL_SetVideoMode(240, 240, BPP_TOSET, SDL_FLAGS);
	
#if defined(WANT_32BPP)
	internal_pix = (uint32_t*)vb_surface->pixels;
#elif defined(WANT_16BPP)
	internal_pix = (uint16_t*)vb_surface->pixels;
#elif defined(WANT_8BPP)
	Set_VB_palette();
	internal_pix = (uint8_t*)vb_surface->pixels;
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
	#ifdef ENABLE_JOYSTICKCODE
	SDL_JoystickClose(sdl_joy);
	#endif
	if (sdl_screen)
	{
		SDL_FreeSurface(sdl_screen);
		sdl_screen = NULL;
	}
	if (vb_surface)
	{
		SDL_FreeSurface(vb_surface);
		vb_surface = NULL;
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
	SDL_BlitSurface(backbuffer, NULL, sdl_screen, NULL);
	SDL_Flip(sdl_screen);
}

void Update_Video_Ingame()
{
	SDL_SoftStretch(vb_surface, NULL, sdl_screen, NULL);
	SDL_Flip(sdl_screen);
}
