#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <libgen.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <SDL/SDL.h>

#include "scaler.h"
#include "font_drawing.h"
#include "sound_output.h"
#include "video_blit.h"
#include "config.h"
#include "menu.h"
#include "main.h"

t_config option;
uint32_t emulator_state = 0;

extern uint8_t exit_vb;

static char home_path[256], save_path[256], sram_path[256], conf_path[256];

extern SDL_Surface *sdl_screen;
extern char GameName_emu[512];

static uint8_t save_slot = 0;
/*static const int8_t upscalers_available = 2
#ifdef SCALE2X_UPSCALER
+1
#endif
;*/

#ifdef FUNKEY
#define MENU_CONFIG_OFFSET_X 43
#else
#define MENU_CONFIG_OFFSET_X 0
#endif

static void SaveState_Menu(uint_fast8_t load_mode, uint_fast8_t slot)
{
	char tmp[512];
	#ifdef CLASSICMAC
	snprintf(tmp, sizeof(tmp), "%s_%d.sts", GameName_emu, slot);
	#else
	snprintf(tmp, sizeof(tmp), "%s/%s_%d.sts", save_path, GameName_emu, slot);
	#endif
	SaveState(tmp,load_mode);
}

static void SRAM_Menu(uint_fast8_t load_mode)
{
	char tmp[512];
	#ifdef CLASSICMAC
	snprintf(tmp, sizeof(tmp), "%s.srm", GameName_emu);
	#else
	snprintf(tmp, sizeof(tmp), "%s/%s.srm", sram_path, GameName_emu);
	#endif
	SRAM_Save(tmp,load_mode);
}


static void config_load()
{
	char config_path[512];
	FILE* fp;
	#ifdef CLASSICMAC
	snprintf(config_path, sizeof(config_path), "%s.cfg", conf_path, GameName_emu);
	#else
	snprintf(config_path, sizeof(config_path), "%s/%s.cfg", conf_path, GameName_emu);
	#endif

	fp = fopen(config_path, "rb");
	if (fp)
	{
		fread(&option, sizeof(option), sizeof(int8_t), fp);
		fclose(fp);
		
		/* Fix it accordingly if previous config file is detected*/
		if (option.config_buttons[1] == SDLK_RIGHT)
		{
			/* Default mapping for Horizontal */
			option.config_buttons[0] = SDLK_UP; // UP
			option.config_buttons[1] = SDLK_DOWN; // DOWN
			option.config_buttons[2] = SDLK_LEFT; // LEFT
			option.config_buttons[3] = SDLK_RIGHT; // RIGHT

			/* Default mapping for Horizontal */
			option.config_buttons[10] = 0; // DPAD2-UP
			option.config_buttons[11] = 0; // DPAD2-DOWN
			option.config_buttons[12] = SDLK_LSHIFT; // DPAD2-LEFT
			option.config_buttons[13] = SDLK_SPACE; // DPAD2-RIGHT	
		}
	}
	else
	{
		/* Default mapping for Horizontal */
		option.config_buttons[0] = SDLK_UP; // UP
		option.config_buttons[1] = SDLK_DOWN; // DOWN
		option.config_buttons[2] = SDLK_LEFT; // LEFT
		option.config_buttons[3] = SDLK_RIGHT; // RIGHT

		option.config_buttons[4] = SDLK_LCTRL; // A
		option.config_buttons[5] = SDLK_LALT; // B

		option.config_buttons[6] = SDLK_TAB; // L
		option.config_buttons[7] = SDLK_BACKSPACE; // R

		option.config_buttons[8] = SDLK_RETURN; // START
		option.config_buttons[9] = SDLK_ESCAPE; // SELECT
		
		/* Default mapping for Horizontal */
		option.config_buttons[10] = 0; // DPAD2-UP
		option.config_buttons[11] = 0; // DPAD2-DOWN
		option.config_buttons[12] = SDLK_LSHIFT; // DPAD2-LEFT
		option.config_buttons[13] = SDLK_SPACE; // DPAD2-RIGHT

		option.fullscreen = 1;
	}
}

static void config_save()
{
	FILE* fp;
	char config_path[512];
	#ifdef CLASSICMAC
	snprintf(config_path, sizeof(config_path), "%s.cfg", conf_path, GameName_emu);
	#else
	snprintf(config_path, sizeof(config_path), "%s/%s.cfg", conf_path, GameName_emu);
	#endif

	fp = fopen(config_path, "wb");
	if (fp)
	{
		fwrite(&option, sizeof(option), sizeof(int8_t), fp);
		fclose(fp);
	}
}

static const char* Return_Text_Button(uint32_t button)
{
	switch(button)
	{
		/* UP button */
		case 273:
			return "D-UP";
		break;
		/* DOWN button */
		case 274:
			return "D-DOWN";
		break;
		/* LEFT button */
		case 276:
			return "D-LEFT";
		break;
		/* RIGHT button */
		case 275:
			return "D-RIGHT";
		break;
		/* A button */
		case 306:
			return "A";
		break;
		/* B button */
		case 308:
			return "B";
		break;
		/* X button */
		case 304:
			return "X";
		break;
		/* Y button */
		case 32:
			return "Y";
		break;
		/* L button */
		case 9:
			return "L";
		break;
		/* R button */
		case 8:
			return "R";
		break;
		/* Power button */
		case 279:
			return "L2";
		break;
		/* Brightness */
		case 51:
			return "R2";
		break;
		/* Volume - */
		case 38:
			return "Volume -";
		break;
		/* Volume + */
		case 233:
			return "Volume +";
		break;
		/* Start */
		case 13:
			return "Start";
		break;
		/* Select */
		case 27:
			return "Select";
		break;
		default:
			return "Unknown";
		break;
		case 0:
			return "...";
		break;
	}
}

static void Input_Remapping()
{
	SDL_Event Event;
	char text[50];
	uint32_t pressed = 0;
	int32_t currentselection = 1;
	int32_t exit_input = 0;
	uint32_t exit_map = 0;

	while(!exit_input)
	{
		SDL_FillRect( backbuffer, NULL, 0 );

        while (SDL_PollEvent(&Event))
        {
            if (Event.type == SDL_KEYDOWN)
            {
                switch(Event.key.keysym.sym)
                {
                    case SDLK_UP:
                        currentselection--;
                        if (currentselection < 1)
                        {
							currentselection = 14;
						}
                        break;
                    case SDLK_DOWN:
                        currentselection++;
                        if (currentselection == 15)
                        {
							currentselection = 1;
						}
                        break;
                    case SDLK_LCTRL:
                    case SDLK_RETURN:
                        pressed = 1;
					break;
                    case SDLK_ESCAPE:
                        option.config_buttons[currentselection - 1] = 0;
					break;
                    case SDLK_LALT:
					case SDLK_p:
                        exit_input = 1;
					break;
                    case SDLK_LEFT:
						if (currentselection > 8) currentselection -= 8;
					break;
                    case SDLK_RIGHT:
						if (currentselection < 9) currentselection += 8;
					break;
					default:
					break;
                }
            }
        }

        if (pressed)
        {
			SDL_Delay(1);
            switch(currentselection)
            {
                default:
					exit_map = 0;
					while( !exit_map )
					{
						SDL_FillRect( backbuffer, NULL, 0 );
						print_string("Please press button for mapping", TextWhite, TextBlue, 37, 108, backbuffer->pixels);

						while (SDL_PollEvent(&Event))
						{
							if (Event.type == SDL_KEYDOWN)
							{
								if (Event.key.keysym.sym != SDLK_RCTRL)
								{
									option.config_buttons[currentselection - 1] = Event.key.keysym.sym;
									exit_map = 1;
									pressed = 0;
								}
							}
						}
						Update_Video_Menu();
					}
				break;
            }
        }

        if (currentselection > 14) currentselection = 14;

		print_string("Press [A] to map to a button", TextWhite, TextBlue, 5, 210, backbuffer->pixels);
		print_string("Press [B] to Exit", TextWhite, TextBlue, 5, 225, backbuffer->pixels);

		snprintf(text, sizeof(text), "UP   : %s\n", Return_Text_Button(option.config_buttons[0]));
		if (currentselection == 1) print_string(text, TextRed, 0, 5, 25+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 25+2, backbuffer->pixels);

		snprintf(text, sizeof(text), "DOWN : %s\n", Return_Text_Button(option.config_buttons[1]));
		if (currentselection == 2) print_string(text, TextRed, 0, 5, 45+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 45+2, backbuffer->pixels);

		snprintf(text, sizeof(text), "LEFT : %s\n", Return_Text_Button(option.config_buttons[2]));
		if (currentselection == 3) print_string(text, TextRed, 0, 5, 65+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 65+2, backbuffer->pixels);

		snprintf(text, sizeof(text), "RIGHT: %s\n", Return_Text_Button(option.config_buttons[3]));
		if (currentselection == 4) print_string(text, TextRed, 0, 5, 85+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 85+2, backbuffer->pixels);

		snprintf(text, sizeof(text), "A    : %s\n", Return_Text_Button(option.config_buttons[4]));
		if (currentselection == 5) print_string(text, TextRed, 0, 5, 105+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 105+2, backbuffer->pixels);

		snprintf(text, sizeof(text), "B    : %s\n", Return_Text_Button(option.config_buttons[5]));
		if (currentselection == 6) print_string(text, TextRed, 0, 5, 125+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 125+2, backbuffer->pixels);

		snprintf(text, sizeof(text), "L    : %s\n", Return_Text_Button(option.config_buttons[6]));
		if (currentselection == 7) print_string(text, TextRed, 0, 5, 145+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 145+2, backbuffer->pixels);

		snprintf(text, sizeof(text), "R    : %s\n", Return_Text_Button(option.config_buttons[7]));
		if (currentselection == 8) print_string(text, TextRed, 0, 5, 165+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 165+2, backbuffer->pixels);

		snprintf(text, sizeof(text), "START  : %s\n", Return_Text_Button(option.config_buttons[8]));
		if (currentselection == 9) print_string(text, TextRed, 0, 165-MENU_CONFIG_OFFSET_X, 25+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 165-MENU_CONFIG_OFFSET_X, 25+2, backbuffer->pixels);

		snprintf(text, sizeof(text), "SELECT : %s\n", Return_Text_Button(option.config_buttons[9]));
		if (currentselection == 10) print_string(text, TextRed, 0, 165-MENU_CONFIG_OFFSET_X, 45+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 165-MENU_CONFIG_OFFSET_X, 45+2, backbuffer->pixels);

		snprintf(text, sizeof(text), "D2-UP  : %s\n", Return_Text_Button(option.config_buttons[10]));
		if (currentselection == 11) print_string(text, TextRed, 0, 165-MENU_CONFIG_OFFSET_X, 65+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 165-MENU_CONFIG_OFFSET_X, 65+2, backbuffer->pixels);

		snprintf(text, sizeof(text), "D2-DOWN : %s\n", Return_Text_Button(option.config_buttons[11]));
		if (currentselection == 12) print_string(text, TextRed, 0, 165-MENU_CONFIG_OFFSET_X, 85+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 165-MENU_CONFIG_OFFSET_X, 85+2, backbuffer->pixels);

		snprintf(text, sizeof(text), "D2-LEFT  : %s\n", Return_Text_Button(option.config_buttons[12]));
		if (currentselection == 13) print_string(text, TextRed, 0, 165-MENU_CONFIG_OFFSET_X, 105+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 165-MENU_CONFIG_OFFSET_X, 105+2, backbuffer->pixels);

		snprintf(text, sizeof(text), "D2-RIGHT : %s\n", Return_Text_Button(option.config_buttons[13]));
		if (currentselection == 14) print_string(text, TextRed, 0, 165-MENU_CONFIG_OFFSET_X, 125+2, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 165-MENU_CONFIG_OFFSET_X, 125+2, backbuffer->pixels);

		Update_Video_Menu();
	}

	config_save();
}

#ifdef SCALING_SOFTWARE
#define SCALING_SOFTWARE_OFFSET 0
#else
#define SCALING_SOFTWARE_OFFSET 1
#endif

void Menu()
{
	char text[50];
    int16_t pressed = 0;
    int16_t currentselection = 1;
    SDL_Event Event;

    Set_Video_Menu();

	/* Save sram settings each time we bring up the menu */
	SRAM_Menu(0);

    while (((currentselection != 1) && (currentselection != 6-SCALING_SOFTWARE_OFFSET)) || (!pressed))
    {
        pressed = 0;

        SDL_FillRect( backbuffer, NULL, 0 );

		print_string("VBEmu - Built on " __DATE__, TextWhite, 0, 5, 15, backbuffer->pixels);

		if (currentselection == 1) print_string("Continue", TextRed, 0, 5, 45, backbuffer->pixels);
		else  print_string("Continue", TextWhite, 0, 5, 45, backbuffer->pixels);

		snprintf(text, sizeof(text), "Load State %d", save_slot);

		if (currentselection == 2) print_string(text, TextRed, 0, 5, 65, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 65, backbuffer->pixels);

		snprintf(text, sizeof(text), "Save State %d", save_slot);

		if (currentselection == 3) print_string(text, TextRed, 0, 5, 85, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 85, backbuffer->pixels);

		#ifdef SCALING_SOFTWARE
        if (currentselection == 4)
        {
			switch(option.fullscreen)
			{
				case 0:
					print_string("Scaling : Native", TextRed, 0, 5, 105, backbuffer->pixels);
				break;
				case 1:
					print_string("Scaling : Stretched", TextRed, 0, 5, 105, backbuffer->pixels);
				break;
				case 2:
					print_string("Scaling : Bilinear", TextRed, 0, 5, 105, backbuffer->pixels);
				break;
				case 3:
					print_string("Scaling : EPX/Scale2x", TextRed, 0, 5, 105, backbuffer->pixels);
				break;
			}
        }
        else
        {
			switch(option.fullscreen)
			{
				case 0:
					print_string("Scaling : Native", TextWhite, 0, 5, 105, backbuffer->pixels);
				break;
				case 1:
					print_string("Scaling : Stretched", TextWhite, 0, 5, 105, backbuffer->pixels);
				break;
				case 2:
					print_string("Scaling : Bilinear", TextWhite, 0, 5, 105, backbuffer->pixels);
				break;
				case 3:
					print_string("Scaling : EPX/Scale2x", TextWhite, 0, 5, 105, backbuffer->pixels);
				break;
			}
        }
		#endif

		if (currentselection == 5-SCALING_SOFTWARE_OFFSET) print_string("Input remapping", TextRed, 0, 5, 125, backbuffer->pixels);
		else print_string("Input remapping", TextWhite, 0, 5, 125, backbuffer->pixels);

		if (currentselection == 6-SCALING_SOFTWARE_OFFSET) print_string("Quit", TextRed, 0, 5, 145, backbuffer->pixels);
		else print_string("Quit", TextWhite, 0, 5, 145, backbuffer->pixels);

		print_string("Libretro Fork by gameblabla", TextWhite, 0, 5, 205, backbuffer->pixels);
		print_string("Credits: Ryphecha, libretro", TextWhite, 0, 5, 225, backbuffer->pixels);

        while (SDL_PollEvent(&Event))
        {
            if (Event.type == SDL_KEYDOWN)
            {
                switch(Event.key.keysym.sym)
                {
                    case SDLK_UP:
                        currentselection--;
                        if (currentselection == 0)
                            currentselection = 6-SCALING_SOFTWARE_OFFSET;
                        break;
                    case SDLK_DOWN:
                        currentselection++;
                        if (currentselection == 7-SCALING_SOFTWARE_OFFSET)
                            currentselection = 1;
                        break;
                    case SDLK_END:
                    case SDLK_RCTRL:
                    case SDLK_LALT:
						pressed = 1;
						currentselection = 1;
						break;
                    case SDLK_LCTRL:
                    case SDLK_RETURN:
                        pressed = 1;
                        break;
                    case SDLK_LEFT:
                        switch(currentselection)
                        {
                            case 2:
                            case 3:
                                if (save_slot > 0) save_slot--;
							break;
							#ifdef SCALING_SOFTWARE
                            case 4:
							option.fullscreen--;
							if (option.fullscreen < 0)
								option.fullscreen = upscalers_available;
							break;
							#endif
                        }
                        break;
                    case SDLK_RIGHT:
                        switch(currentselection)
                        {
                            case 2:
                            case 3:
                                save_slot++;
								if (save_slot == 10)
									save_slot = 9;
							break;
							#ifdef SCALING_SOFTWARE
                            case 4:
                                option.fullscreen++;
                                if (option.fullscreen > upscalers_available)
                                    option.fullscreen = 0;
							break;
							#endif
                        }
                        break;
					default:
					break;
                }
            }
            else if (Event.type == SDL_QUIT)
            {
				currentselection = 6-SCALING_SOFTWARE_OFFSET;
				pressed = 1;
			}
        }

        if (pressed)
        {
            switch(currentselection)
            {
				case 5-SCALING_SOFTWARE_OFFSET:
					Input_Remapping();
				break;
				#ifdef SCALING_SOFTWARE
                case 4 :
                    option.fullscreen++;
                    if (option.fullscreen > upscalers_available)
                        option.fullscreen = 0;
                    break;
                #endif
                case 2 :
                    SaveState_Menu(1, save_slot);
					currentselection = 1;
                    break;
                case 3 :
					SaveState_Menu(0, save_slot);
					currentselection = 1;
				break;
				default:
				break;
            }
        }

		Update_Video_Menu();
    }

    SDL_FillRect(sdl_screen, NULL, 0);
    SDL_Flip(sdl_screen);
    #ifdef SDL_TRIPLEBUF
    SDL_FillRect(sdl_screen, NULL, 0);
    SDL_Flip(sdl_screen);
    #endif

    if (currentselection == 6-SCALING_SOFTWARE_OFFSET)
    {
        exit_vb = 1;
	}

	/* Switch back to emulator core */
	emulator_state = 0;
	Set_Video_InGame();
}

void Init_Configuration()
{
	#ifdef CLASSICMAC
	snprintf(home_path, sizeof(home_path), "");

	snprintf(conf_path, sizeof(conf_path), "%s", home_path);
	snprintf(save_path, sizeof(save_path), "%s", home_path);
	snprintf(sram_path, sizeof(sram_path), "%s", home_path);
	#else
	snprintf(home_path, sizeof(home_path), "%s/.vbemu", getenv("HOME"));

	snprintf(conf_path, sizeof(conf_path), "%s/conf", home_path);
	snprintf(save_path, sizeof(save_path), "%s/sstates", home_path);
	snprintf(sram_path, sizeof(sram_path), "%s/sram", home_path);

	/* We check first if folder does not exist.
	 * Let's only try to create it if so in order to decrease boot times.
	 * */

	
	if (access( home_path, F_OK ) == -1)
	{
		mkdir(home_path, 0755);
	}

	if (access( save_path, F_OK ) == -1)
	{
		mkdir(save_path, 0755);
	}

	if (access( conf_path, F_OK ) == -1)
	{
		mkdir(conf_path, 0755);
	}

	if (access( sram_path, F_OK ) == -1)
	{
		mkdir(sram_path, 0755);
	}
	#endif

	/* Load sram file if it exists */
	SRAM_Menu(1);

	config_load();
}
