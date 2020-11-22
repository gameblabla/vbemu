#ifndef MENU_H
#define MENU_H

#ifdef __cplusplus
  extern "C" {
#endif

#include <stdint.h>

#ifndef PATH_MAX
#define PATH_MAX 2048
#endif

#define RGB565(r,g,b) ((r << 8) | (g << 3) | (b >> 3))

extern uint8_t exit_vb;
extern uint32_t emulator_state;
extern uint32_t done;

extern void Menu(void);
extern void Init_Configuration(void);

#ifdef __cplusplus
  }
#endif

#endif
