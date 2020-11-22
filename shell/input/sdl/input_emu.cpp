#include <SDL/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include "main.h"

#include "menu.h"
#include "config.h"

uint8_t *keystate;
extern uint8_t exit_vb;
extern uint32_t emulator_state;

#ifdef ENABLE_JOYSTICKCODE
#define joy_commit_range 8192
int32_t axis_input[4] = {0, 0, 0, 0};
#endif

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
#ifdef GKD350_BUG_INPUT
					case SDLK_LSHIFT:
#endif
					case SDLK_RCTRL:
					case SDLK_END:
						emulator_state = 1;
					break;
					default:
					break;
				}
			break;
			case SDL_KEYUP:
				switch(event.key.keysym.sym)
				{
					case SDLK_HOME:
						emulator_state = 1;
					break;
					default:
					break;
				}
			break;
			#ifdef ENABLE_JOYSTICKCODE
			case SDL_JOYAXISMOTION:
				if (event.jaxis.axis < 4)
				axis_input[event.jaxis.axis] = event.jaxis.value;
			break;
			#endif
			default:
			break;
		}
	}
	
	// UP
	if (keys[option.config_buttons[0] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[1] < -joy_commit_range
#endif
	) button |= 512;
	
	// DOWN
	if (keys[option.config_buttons[1] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[1] > joy_commit_range
#endif
	) button |= 256;
	
	// LEFT
	if (keys[option.config_buttons[2] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[0] < -joy_commit_range
#endif
	) button |= 128;
	
	// RIGHT
	if (keys[option.config_buttons[3] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[0] > joy_commit_range
#endif
	) button |= 64;
	
	
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
	
	
	/* Second axis is offset by one on the RG-350 */
	
	// DPAD2-UP
	if (keys[option.config_buttons[10] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[4] < -joy_commit_range
#endif
	) button |= 16;
	
	// DPAD2-DOWN
	if (keys[option.config_buttons[11] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[4] > joy_commit_range
#endif
	) button |= 8192;
	
	// DPAD2-LEFT
	if (keys[option.config_buttons[12] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[3] < -joy_commit_range
#endif
	) button |= 4096;
	
	// DPAD2-RIGHT
	if (keys[option.config_buttons[13] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[3] > joy_commit_range
#endif
	) button |= 32;
	
	
	return button;
}
