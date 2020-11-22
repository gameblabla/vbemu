#include <SDL/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include "main.h"

#include "menu.h"
#include "config.h"

uint8_t *keystate;
extern uint8_t exit_vb;
extern uint32_t emulator_state;

uint16_t Read_Input(void)
{
	uint16_t button = 0;
	SDL_Event event;
	uint8_t* keys;
	
	keys = SDL_GetKeyState(NULL);

	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
			case SDL_KEYDOWN:
				switch(event.key.keysym.sym)
				{
					case SDLK_RCTRL:
					case SDLK_END:
					case SDLK_ESCAPE:
						emulator_state = 1;
					break;
				}
			break;
			case SDL_KEYUP:
				switch(event.key.keysym.sym)
				{
					case SDLK_HOME:
						emulator_state = 1;
					break;
				}
			break;
		}
	}
	
	// UP
	if (keys[option.config_buttons[0] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[1] < -joy_commit_range
#endif
	) button |= 512;
	
	// RIGHT
	if (keys[option.config_buttons[1] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[1] > joy_commit_range
#endif
	) button |= 64;
	
	// DOWN
	if (keys[option.config_buttons[2] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[0] < -joy_commit_range
#endif
	) button |= 256;
	
	// LEFT
	if (keys[option.config_buttons[3] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[0] > joy_commit_range
#endif
	) button |= 128;
	
	// A
	if (keys[option.config_buttons[4] ] == SDL_PRESSED) button |= 1;
	// B
	if (keys[option.config_buttons[5] ] == SDL_PRESSED) button |= 2;
	
	// L
	if (keys[option.config_buttons[6] ] == SDL_PRESSED) button |= 4;
	// R
	if (keys[option.config_buttons[7] ] == SDL_PRESSED) button |= 8;

	// Start
	if (keys[option.config_buttons[8] ] == SDL_PRESSED) button |= 1024;
	// Select
	if (keys[option.config_buttons[9] ] == SDL_PRESSED) button |= 2048;
	
	return button;
}
