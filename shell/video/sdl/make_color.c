#ifdef WANT_8BPP
#include <SDL/SDL.h>
#include <stdio.h>

SDL_Color colors[256];

extern SDL_Surface * sdl_screen;

static void Set_Red(uint8_t r, uint16_t entry)
{
	if (entry > 255) return;
	colors[entry].r = r;
	colors[entry].g = r;
	colors[entry].b = r;
}

void Set_VB_palette(void)
{
	uint8_t i;
	for(i=0;i<86;i++)
	{
		Set_Red(0, (i*4));
		Set_Red(227-i, (i*4)+1);
		Set_Red(149-i, (i*4)+2);
		Set_Red(86-i, (i*4)+3);
	}
	SDL_SetPalette(sdl_screen, SDL_LOGPAL|SDL_PHYSPAL, colors, 0, 256);
}


uint8_t Make_Color(uint8_t r, uint8_t g, uint8_t b)
{
	return SDL_MapRGB(sdl_screen->format, r, g, b);
}
#endif
