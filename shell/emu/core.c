#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#include <sys/time.h>
#include <stdarg.h>
#include <iconv.h>
#include "mednafen/git.h"
#include "mednafen/state_helpers.h"
#include "mednafen/masmem.h"

#include "shared.h"
#include "video_blit.h"
#include "sound_output.h"
#include "menu.h"
#include "input_emu.h"

/* Forward declarations */
void MDFN_LoadGameCheats(void *override);
void MDFN_FlushGameCheats(int nosave);

static struct MDFN_Surface surf;

char GameName_emu[512];
extern uint32_t emulator_state;

static uint64_t video_frames = 0;

uint8_t exit_vb = 0;

/* Mednafen - Multi-system Emulator
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


#include "mednafen/vb/vb.h"
#include "mednafen/vb/timer.h"
#include "mednafen/vb/vsu.h"
#include "mednafen/vb/vip.h"
#include "mednafen/vb/input.h"
#include "mednafen/hw_cpu/v810/v810_cpu.h"

#include "games_database_patch.h"

#define STICK_DEADZONE 0x4000
#define RIGHT_DPAD_LEFT 0x1000
#define RIGHT_DPAD_RIGHT 0x0020
#define RIGHT_DPAD_UP 0x0010
#define RIGHT_DPAD_DOWN 0x2000

static Blip_Buffer sbuf[2];

static uint8_t *WRAM = NULL;

static uint8_t *GPRAM = NULL;
static uint32 GPRAM_Mask;

static uint8_t *GPROM = NULL;
static uint32 GPROM_Mask;

static uint32 VSU_CycleFix;

static uint8_t WCR;

static int32 next_vip_ts, next_timer_ts, next_input_ts;

static uint32 IRQ_Asserted;

MDFNGI EmulatedVB =
{
	MDFN_MASTERCLOCK_FIXED(VB_MASTER_CLOCK),
	0,
	0,   // lcm_width
	0,   // lcm_height
	384,   // Nominal width
	224,   // Nominal height
	384,   // Framebuffer width
	224,   // Framebuffer height
	2,     // Number of output sound channels
	44100, // Sound frequency
};

MDFNGI *MDFNGameInfo = &EmulatedVB;

static INLINE void RecalcIntLevel(void)
{
   int ilevel = -1;

   for(int i = 4; i >= 0; i--)
   {
      if(IRQ_Asserted & (1 << i))
      {
         ilevel = i;
         break;
      }
   }

   V810_SetInt(ilevel);
}

void VBIRQ_Assert(int source, bool assert)
{
   /*assert(source >= 0 && source <= 4);*/

   IRQ_Asserted &= ~(1 << source);

   if(assert)
      IRQ_Asserted |= 1 << source;

   RecalcIntLevel();
}

static uint8_t HWCTRL_Read(v810_timestamp_t timestamp, uint32 A)
{
   uint8_t ret = 0;

   /* HWCtrl Bogus Read? */
   if(A & 0x3)
      return(ret);

   switch(A & 0xFF)
   {
      default:
#if 0
         printf("Unknown HWCTRL Read: %08x\n", A);
#endif
         break;

      case 0x18:
      case 0x1C:
      case 0x20: ret = TIMER_Read(timestamp, A);
                 break;

      case 0x24: ret = WCR | 0xFC;
                 break;

      case 0x10:
      case 0x14:
      case 0x28: ret = VBINPUT_Read(timestamp, A);
                 break;

   }

   return(ret);
}

static void HWCTRL_Write(v810_timestamp_t timestamp, uint32 A, uint8_t V)
{
   /* HWCtrl Bogus Write? */
   if(A & 0x3)
      return;

   switch(A & 0xFF)
   {
      default:
         //printf("Unknown HWCTRL Write: %08x %02x\n", A, V);
         break;

      case 0x18:
      case 0x1C:
      case 0x20: TIMER_Write(timestamp, A, V);
                 break;

      case 0x24: WCR = V & 0x3;
                 break;

      case 0x10:
      case 0x14:
      case 0x28: VBINPUT_Write(timestamp, A, V);
                 break;
   }
}

uint8 MDFN_FASTCALL MemRead8(v810_timestamp_t timestamp, uint32 A)
{
   uint8 ret = 0;
   A &= (1 << 27) - 1;

   //if((A >> 24) <= 2)
   // printf("Read8: %d %08x\n", timestamp, A);

   switch(A >> 24)
   {
      case 0: ret = VIP_Read8(timestamp, A);
              break;

      case 1: break;

      case 2: ret = HWCTRL_Read(timestamp, A);
              break;

      case 3: break;
      case 4: break;

      case 5: ret = WRAM[A & 0xFFFF];
              break;

      case 6: if(GPRAM)
                 ret = GPRAM[A & GPRAM_Mask];
#if 0
              else
                 printf("GPRAM(Unmapped) Read: %08x\n", A);
#endif
              break;

      case 7: ret = GPROM[A & GPROM_Mask];
              break;
   }
   return(ret);
}

uint16 MDFN_FASTCALL MemRead16(v810_timestamp_t timestamp, uint32 A)
{
 uint16 ret = 0;

 A &= (1 << 27) - 1;

 //if((A >> 24) <= 2)
 // printf("Read16: %d %08x\n", timestamp, A);


 switch(A >> 24)
 {
  case 0: ret = VIP_Read16(timestamp, A);
      break;

  case 1: break;

  case 2: ret = HWCTRL_Read(timestamp, A);
      break;

  case 3: break;

  case 4: break;

  case 5: ret = LoadU16_LE((uint16 *)&WRAM[A & 0xFFFF]);
      break;

  case 6: if(GPRAM)
           ret = LoadU16_LE((uint16 *)&GPRAM[A & GPRAM_Mask]);
#if 0
      else printf("GPRAM(Unmapped) Read: %08x\n", A);
#endif
      break;

  case 7: ret = LoadU16_LE((uint16 *)&GPROM[A & GPROM_Mask]);
      break;
 }
 return(ret);
}

void MDFN_FASTCALL MemWrite8(v810_timestamp_t timestamp, uint32 A, uint8_t V)
{
 A &= (1 << 27) - 1;

 //if((A >> 24) <= 2)
 // printf("Write8: %d %08x %02x\n", timestamp, A, V);

 switch(A >> 24)
 {
  case 0: VIP_Write8(timestamp, A, V);
          break;

  case 1: VSU_Write((timestamp + VSU_CycleFix) >> 2, A, V);
          break;

  case 2: HWCTRL_Write(timestamp, A, V);
          break;

  case 3: break;

  case 4: break;

  case 5: WRAM[A & 0xFFFF] = V;
          break;

  case 6: if(GPRAM)
           GPRAM[A & GPRAM_Mask] = V;
          break;

  case 7: // ROM, no writing allowed!
          break;
 }
}

void MDFN_FASTCALL MemWrite16(v810_timestamp_t timestamp, uint32 A, uint16 V)
{
 A &= (1 << 27) - 1;

 //if((A >> 24) <= 2)
 // printf("Write16: %d %08x %04x\n", timestamp, A, V);

 switch(A >> 24)
 {
  case 0: VIP_Write16(timestamp, A, V);
          break;

  case 1: VSU_Write((timestamp + VSU_CycleFix) >> 2, A, V);
          break;

  case 2: HWCTRL_Write(timestamp, A, V);
          break;

  case 3: break;

  case 4: break;

  case 5: StoreU16_LE((uint16 *)&WRAM[A & 0xFFFF], V);
          break;

  case 6: if(GPRAM)
           StoreU16_LE((uint16 *)&GPRAM[A & GPRAM_Mask], V);
          break;

  case 7: // ROM, no writing allowed!
          break;
 }
}

static void FixNonEvents(void)
{
	if(next_vip_ts & 0x40000000) next_vip_ts = VB_EVENT_NONONO;
	if(next_timer_ts & 0x40000000) next_timer_ts = VB_EVENT_NONONO;
	if(next_input_ts & 0x40000000) next_input_ts = VB_EVENT_NONONO;
}

static void EventReset(void)
{
	next_vip_ts = VB_EVENT_NONONO;
	next_timer_ts = VB_EVENT_NONONO;
	next_input_ts = VB_EVENT_NONONO;
}

static INLINE int32 CalcNextTS(void)
{
	int32 next_timestamp = next_vip_ts;

	if(next_timestamp > next_timer_ts)
		next_timestamp  = next_timer_ts;

	if(next_timestamp > next_input_ts)
		next_timestamp  = next_input_ts;

	return(next_timestamp);
}

static void RebaseTS(const v810_timestamp_t timestamp)
{
	//printf("Rebase: %08x %08x %08x\n", timestamp, next_vip_ts, next_timer_ts);

	//assert(next_vip_ts > timestamp);
	//assert(next_timer_ts > timestamp);
	//assert(next_input_ts > timestamp);

	next_vip_ts -= timestamp;
	next_timer_ts -= timestamp;
	next_input_ts -= timestamp;
}

void VB_SetEvent(const int type, const v810_timestamp_t next_timestamp)
{
   //assert(next_timestamp > V810_v810_timestamp);

	if(type == VB_EVENT_VIP)
		next_vip_ts = next_timestamp;
	else if(type == VB_EVENT_TIMER)
		next_timer_ts = next_timestamp;
	else if(type == VB_EVENT_INPUT)
		next_input_ts = next_timestamp;

	if(next_timestamp < V810_GetEventNT())
		V810_SetEventNT(next_timestamp);
}

static int32 MDFN_FASTCALL EventHandler(const v810_timestamp_t timestamp)
{
	if(timestamp >= next_vip_ts)
		next_vip_ts = VIP_Update(timestamp);

	if(timestamp >= next_timer_ts)
		next_timer_ts = TIMER_Update(timestamp);

	if(timestamp >= next_input_ts)
		next_input_ts = VBINPUT_Update(timestamp);

	return(CalcNextTS());
}

// Called externally from debug.cpp in some cases.
static void ForceEventUpdates(const v810_timestamp_t timestamp)
{
	next_vip_ts = VIP_Update(timestamp);
	next_timer_ts = TIMER_Update(timestamp);
	next_input_ts = VBINPUT_Update(timestamp);

	V810_SetEventNT(CalcNextTS());
	//printf("FEU: %d %d %d\n", next_vip_ts, next_timer_ts, next_input_ts);
}

static void VB_Power(void)
{
	memset(WRAM, 0, 65536);

	VIP_Power();
	VSU_Power();
	TIMER_Power();
	VBINPUT_Power();

	EventReset();
	IRQ_Asserted = 0;
	RecalcIntLevel();
	V810_Reset();

	VSU_CycleFix = 0;
	WCR = 0;

	ForceEventUpdates(0);  //V810_v810_timestamp);
}

// Source: http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
// Rounds up to the nearest power of 2.
static INLINE uint32 round_up_pow2(uint32 v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	v += (v == 0);
	return(v);
}

#define DEF_VECTOR(type) \
typedef struct vector_##type { \
  type * data; \
  size_t alloc_size; \
  size_t size; \
} vector_##type; \
\
void init_vector_##type(vector_##type * vector) \
{ \
  vector->data = NULL; \
  vector->alloc_size = vector->size = 0; \
} \
\
void clear_vector_##type(vector_##type * vector) \
{ \
  if (vector->data != NULL) {\
    free(vector->data); \
    init_vector_##type(vector);  \
  } \
} \
\
void push_back_vector_##type(vector_##type * vector, type value) \
{ \
  if (vector->size == vector->alloc_size) { \
    vector->alloc_size = (vector->alloc_size == 0) ? 16 : vector->alloc_size * 2; \
    vector->data = realloc(vector->data, vector->alloc_size * sizeof(type)); \
  } \
  vector->data[vector->size++] = value; \
} \

DEF_VECTOR(uint32)

static int Load(const uint8_t *data, size_t size)
{
   /* VB ROM image size is not a power of 2??? */
   if(size != round_up_pow2(size))
      return(0);

   /* VB ROM image size is too small?? */
   if(size < 256)
      return(0);

   /* VB ROM image size is too large?? */
   if(size > (1 << 24))
      return(0);

   printf("ROM:       %dKiB\n", (int)(size / 1024));

   V810_Init();

   /*V810_SetMemReadHandlers(MemRead8, MemRead16, NULL);
   V810_SetMemWriteHandlers(MemWrite8, MemWrite16, NULL);*/
   V810_SetIOReadHandlers(MemRead8, MemRead16, NULL);
   V810_SetIOWriteHandlers(MemWrite8, MemWrite16, NULL);

   for(int i = 0; i < 256; i++)
   {
      V810_SetMemReadBus32(i, false);
      V810_SetMemWriteBus32(i, false);
   }

   vector_uint32 Map_Addresses;
   init_vector_uint32(&Map_Addresses);

   for(uint64 A = 0; A < 1ULL << 32; A += (1 << 27))
   {
      for(uint64 sub_A = 5 << 24; sub_A < (6 << 24); sub_A += 65536)
      {
         push_back_vector_uint32(&Map_Addresses, A + sub_A);
      }
   }

   WRAM = V810_SetFastMap(&Map_Addresses.data[0], 65536, Map_Addresses.size, "WRAM");
   clear_vector_uint32(&Map_Addresses);

   // Round up the ROM size to 65536(we mirror it a little later)
   GPROM_Mask = (size < 65536) ? (65536 - 1) : (size - 1);

   for(uint64 A = 0; A < 1ULL << 32; A += (1 << 27))
   {
      for(uint64 sub_A = 7 << 24; sub_A < (8 << 24); sub_A += GPROM_Mask + 1)
      {
         push_back_vector_uint32(&Map_Addresses, A + sub_A);
         //printf("%08x\n", (uint32)(A + sub_A));
      }
   }


   GPROM = V810_SetFastMap(&Map_Addresses.data[0], GPROM_Mask + 1, Map_Addresses.size, "Cart ROM");
   clear_vector_uint32(&Map_Addresses);

   // Mirror ROM images < 64KiB to 64KiB
   for(uint64 i = 0; i < 65536; i += size)
      memcpy(GPROM + i, data, size);

   GPRAM_Mask = 0xFFFF;

   for(uint64 A = 0; A < 1ULL << 32; A += (1 << 27))
   {
      for(uint64 sub_A = 6 << 24; sub_A < (7 << 24); sub_A += GPRAM_Mask + 1)
      {
         //printf("GPRAM: %08x\n", A + sub_A);
         push_back_vector_uint32(&Map_Addresses, A + sub_A);
      }
   }


   GPRAM = V810_SetFastMap(&Map_Addresses.data[0], GPRAM_Mask + 1, Map_Addresses.size, "Cart RAM");
   clear_vector_uint32(&Map_Addresses);

   memset(GPRAM, 0, GPRAM_Mask + 1);

   VIP_Init();
   VSU_Init(&sbuf[0], &sbuf[1]);
   VBINPUT_Init();

   VIP_Set3DMode(1, 0);

   MDFNGameInfo->fps = (int64)20000000 * 65536 * 256 / (259 * 384 * 4);


   VB_Power();

   MDFNGameInfo->nominal_width = 384;
   MDFNGameInfo->nominal_height = 224;
   MDFNGameInfo->fb_width = 384;
   MDFNGameInfo->fb_height = 224;

   MDFNGameInfo->lcm_width = MDFNGameInfo->fb_width;
   MDFNGameInfo->lcm_height = MDFNGameInfo->fb_height;

   return(1);
}

static void CloseGame(void)
{
   //VIP_Kill();

   /*
      if(GPRAM)
      {
      MDFN_free(GPRAM);
      GPRAM = NULL;
      }

      if(GPROM)
      {
      MDFN_free(GPROM);
      GPROM = NULL;
      }
      */
}

void VB_ExitLoop(void)
{
   V810_Exit();
}

static void Emulate(EmulateSpecStruct *espec, int16_t *sound_buf)
{
	v810_timestamp_t v810_timestamp;

	VBINPUT_Frame();

	VIP_StartFrame(espec);

	v810_timestamp = V810_Run(EventHandler);

	FixNonEvents();
	ForceEventUpdates(v810_timestamp);

	VSU_EndFrame((v810_timestamp + VSU_CycleFix) >> 2);

	if(sound_buf)
	{
		for(int y = 0; y < 2; y++)
		{
			Blip_Buffer_end_frame(&sbuf[y], (v810_timestamp + VSU_CycleFix) >> 2);
			espec->SoundBufSize = Blip_Buffer_read_samples(&sbuf[y], sound_buf + y, espec->SoundBufMaxSize);
		}
	}

   VSU_CycleFix = (v810_timestamp + VSU_CycleFix) & 3;

   espec->MasterCycles = v810_timestamp;

   TIMER_ResetTS();
   VBINPUT_ResetTS();
   VIP_ResetTS();

   RebaseTS(v810_timestamp);

   V810_ResetTS(0);
}

int StateAction(StateMem *sm, int load, int data_only)
{
   const v810_timestamp_t timestamp = v810_timestamp;
   int ret = 1;

   SFORMAT StateRegs[] =
   {
      SFARRAY(WRAM, 65536),
      SFARRAY(GPRAM, GPRAM_Mask ? (GPRAM_Mask + 1) : 0),
      SFVARN(WCR, "WCR"),
      SFVARN(IRQ_Asserted, "IRQ_Asserted"),
      SFVARN(VSU_CycleFix, "VSU_CycleFix"),
      SFEND
   };

   ret &= MDFNSS_StateAction(sm, load, data_only, StateRegs, "MAIN", false);

   ret &= V810_StateAction(sm, load, data_only);

   ret &= VSU_StateAction(sm, load, data_only);
   ret &= TIMER_StateAction(sm, load, data_only);
   ret &= VBINPUT_StateAction(sm, load, data_only);
   ret &= VIP_StateAction(sm, load, data_only);

   if(load)
   {
      // Needed to recalculate next_*_ts since we don't bother storing their deltas in save states.
      ForceEventUpdates(timestamp);
   }
   return(ret);
}

static void DoSimpleCommand(int cmd)
{
   switch(cmd)
   {
      case MDFN_MSC_POWER:
      case MDFN_MSC_RESET:
         VB_Power();
         break;
   }
}

static bool MDFNI_LoadGame(const uint8_t *data, size_t size)
{
   MDFNGameInfo = &EmulatedVB;

   if(Load(data, size) <= 0)
      goto error;

   return true;

error:
   printf("Can't load ROM\n");
   MDFNGameInfo = NULL;
   return false;
}

static void MDFNI_CloseGame(void)
{
   if(!MDFNGameInfo)
      return;


   CloseGame();

   MDFNGameInfo = NULL;
}

static void hookup_ports(bool force);

static bool initial_ports_hookup = false;

#define MEDNAFEN_CORE_NAME_MODULE "vb"
#define MEDNAFEN_CORE_NAME "Beetle VB"
#define MEDNAFEN_CORE_VERSION "v0.9.36.1"
#define MEDNAFEN_CORE_EXTENSIONS "vb|vboy|bin"
#define MEDNAFEN_CORE_TIMING_FPS 50.27
#define MEDNAFEN_CORE_GEOMETRY_BASE_W (EmulatedVB.nominal_width)
#define MEDNAFEN_CORE_GEOMETRY_BASE_H (EmulatedVB.nominal_height)
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 384
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 224
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (12.0 / 7.0)
#define FB_WIDTH 384
#define FB_HEIGHT 224

#define FB_MAX_HEIGHT FB_HEIGHT


void Reset_VB(void)
{
   DoSimpleCommand(MDFN_MSC_RESET);
}

static void check_variables(void)
{
	for(uint_fast8_t y = 0; y < 2; y++)
	{
		Blip_Buffer_set_sample_rate(&sbuf[y], SOUND_OUTPUT_FREQUENCY, 50);
		Blip_Buffer_set_clock_rate(&sbuf[y], (long)(VB_MASTER_CLOCK / 4));
		Blip_Buffer_bass_freq(&sbuf[y], 20);
	}
	VIP_SetDefaultColor(0xAA0000);
}

#define MAX_PLAYERS 1
#define MAX_BUTTONS 14
static uint16_t input_buf[MAX_PLAYERS];

static void hookup_ports(bool force)
{
   if (initial_ports_hookup && !force)
      return;

   /* Possible endian bug ... */
   VBINPUT_SetInput(0, "gamepad", &input_buf[0]);

   initial_ports_hookup = true;
}

bool Load_Game_Memory(char* game_name)
{
	uint8_t* rom_data;
	size_t length;
	FILE* fp;
	
	check_variables();

	fp = fopen(game_name, "rb");
	if (!fp) return 0;
	fseek (fp, 0, SEEK_END);   // non-portable
	length = ftell(fp);
	rom_data = (uint8_t *)malloc(length);
	fseek (fp, 0, SEEK_SET);
	fread (rom_data, sizeof(uint8_t), length, fp);
	fclose (fp);

	if (!MDFNI_LoadGame(rom_data,length))
		return false;

	if (rom_data)
	{
		free(rom_data);
		rom_data = NULL;
	}

	struct MDFN_PixelFormat pix_fmt;
#ifdef WANT_16BPP
	extern SDL_Surface* sdl_screen;
	pix_fmt.bpp        = 16;
	pix_fmt.Rshift     = sdl_screen->format->Rshift;
	pix_fmt.Gshift     = sdl_screen->format->Gshift;
	pix_fmt.Bshift     = sdl_screen->format->Bshift;
	pix_fmt.Ashift     = sdl_screen->format->Ashift;
#elif defined(WANT_8BPP)
	extern SDL_Surface* sdl_screen;
	pix_fmt.bpp        = 8;
	pix_fmt.Rshift     = sdl_screen->format->Rshift;
	pix_fmt.Gshift     = sdl_screen->format->Gshift;
	pix_fmt.Bshift     = sdl_screen->format->Bshift;
	pix_fmt.Ashift     = sdl_screen->format->Ashift;
#else
	pix_fmt.bpp        = 32;
	pix_fmt.Rshift     = 16;
	pix_fmt.Gshift     = 8;
	pix_fmt.Bshift     = 0;
	pix_fmt.Ashift     = 24;
#endif
	pix_fmt.colorspace = MDFN_COLORSPACE_RGB;

	surf.format                  = pix_fmt;
	surf.pixels                  = (WIDTH_TYPE *)internal_pix;
	surf.w                       = FB_WIDTH;
	surf.h                       = FB_HEIGHT;
	surf.pitchinpix              = FB_WIDTH;
	hookup_ports(true);

	return true;
}

void retro_unload_game(void)
{
   MDFNI_CloseGame();
}

static uint64_t audio_frames;

#ifdef FRAMESKIP
static uint32_t Timer_Read(void) 
{
	/* Timing. */
	struct timeval tval;
  	gettimeofday(&tval, 0);
	return (((tval.tv_sec*1000000) + (tval.tv_usec)));
}
static long lastTick = 0, newTick;
static uint32_t SkipCnt = 0, FPS = MEDNAFEN_CORE_TIMING_FPS, FrameSkip;
static const uint32_t TblSkip[5][5] = {
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 1},
    {0, 0, 0, 1, 1},
    {0, 0, 1, 1, 1},
    {0, 1, 1, 1, 1},
};
#endif

void Emulation_Run(void)
{
	static int16_t sound_buf[0x10000];
	static MDFN_Rect rects[FB_MAX_HEIGHT];
	rects[0].w = ~0;

	EmulateSpecStruct spec = {0};
	spec.surface    = &surf;
	spec.SoundRate  = SOUND_OUTPUT_FREQUENCY;
	spec.LineWidths = rects;
	spec.SoundBufMaxSize = sizeof(sound_buf) / 2;
	spec.SoundBufSize = 0;
	spec.VideoFormatChanged = false;
	spec.SoundFormatChanged = false;
#ifdef FRAMESKIP
	SkipCnt++;
	if (SkipCnt > 4) SkipCnt = 0;
	spec.skip = TblSkip[FrameSkip][SkipCnt];
#else
	spec.skip = 0;
#endif
	input_buf[0] = Read_Input();
	Emulate(&spec, sound_buf);

	//int16 *const SoundBuf = sound_buf + spec.SoundBufSizeALMS * EmulatedVB.soundchan;
	int32 SoundBufSize = spec.SoundBufSize - spec.SoundBufSizeALMS;
	//const int32 SoundBufMaxSize = spec.SoundBufMaxSize - spec.SoundBufSizeALMS;
	spec.SoundBufSize = spec.SoundBufSizeALMS + SoundBufSize;
   
#ifdef FRAMESKIP
	newTick = Timer_Read();
	if ( (newTick) - (lastTick) > 1000000) 
	{
		FPS = video_frames;
		video_frames = 0;
		lastTick = newTick;
		if (FPS >= 50)
		{
			FrameSkip = 0;
		}
		else
		{
			FrameSkip = MEDNAFEN_CORE_TIMING_FPS / FPS;
			if (FrameSkip > 4) FrameSkip = 4;
		}
	}
#endif

	Audio_Write((int16_t*) sound_buf, spec.SoundBufSize);
	audio_frames += spec.SoundBufSize;
	Update_Video_Ingame();
	
#ifdef FRAMESKIP
	if (spec.skip == false) video_frames++;
#else
	video_frames++;
#endif
}


void Clean(void)
{
   surf.pixels     = NULL;
   surf.w          = 0;
   surf.h          = 0;
   surf.pitchinpix = 0;
   surf.format.bpp        = 0;
   surf.format.colorspace = 0;
   surf.format.Rshift     = 0;
   surf.format.Gshift     = 0;
   surf.format.Bshift     = 0;
   surf.format.Ashift     = 0;
}


size_t retro_serialize_size(void)
{
   StateMem st;

   st.data           = NULL;
   st.loc            = 0;
   st.len            = 0;
   st.malloced       = 0;
   st.initial_malloc = 0;

   if (!MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL))
      return 0;

   free(st.data);
   return st.len;
}

bool retro_serialize(void *data, size_t size)
{
   StateMem st;
   bool ret          = false;
   uint8_t *_dat     = (uint8_t*)malloc(size);

   if (!_dat)
      return false;

   /* Mednafen can realloc the buffer so we need to ensure this is safe. */
   st.data           = _dat;
   st.loc            = 0;
   st.len            = 0;
   st.malloced       = size;
   st.initial_malloc = 0;

   ret = MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL);

   memcpy(data, st.data, size);
   free(st.data);

   return ret;
}

bool retro_unserialize(const void *data, size_t size)
{
   StateMem st;

   st.data           = (uint8_t*)data;
   st.loc            = 0;
   st.len            = size;
   st.malloced       = 0;
   st.initial_malloc = 0;

   return MDFNSS_LoadSM(&st, 0, 0);
}

void *retro_get_memory_data(unsigned type)
{
   switch(type)
   {
      case 0:
         return WRAM;
      case 1:
         return GPRAM;
      default:
         break;
   }

   return NULL;
}

size_t retro_get_memory_size(unsigned type)
{
   switch(type)
   {
      case 0:
         return 0x10000;
      case 1:
         return GPRAM_Mask + 1;
      default:
         break;
   }

   return 0;
}

void SaveState(char* path, uint_fast8_t state)
{	
	FILE* savefp;
	size_t file_size;
	char* buffer = NULL;
	if (state == 1)
	{
		savefp = fopen(path, "rb");
		if (savefp)
		{
			fseek(savefp, 0, SEEK_END);
			file_size = ftell(savefp);
			fseek(savefp, 0, SEEK_SET);
			if (file_size > 0)
			{
				buffer = (char*)malloc(file_size);
				fread(buffer, sizeof(uint8_t), file_size, savefp);
				retro_unserialize(buffer, file_size);
			}
			fclose(savefp);
		}
	}
	else
	{
		savefp = fopen(path, "wb");
		if (savefp)
		{
			file_size = retro_serialize_size();
			buffer = (char*)malloc(file_size);
			retro_serialize(buffer, file_size);
			fwrite(buffer, sizeof(uint8_t), file_size, savefp);
			fclose(savefp);
		}
	}
	
	if (buffer)
	{
		free(buffer);
		buffer = NULL;
	}
}


void SRAM_Save(char* path, uint_fast8_t state)
{	
	FILE* savefp;
	size_t file_size = 0;
	if (state == 1)
	{
		savefp = fopen(path, "rb");
		if (savefp)
		{
			fseek(savefp, 0, SEEK_END);
			file_size = ftell(savefp);
			fseek(savefp, 0, SEEK_SET);
			fread(GPRAM, sizeof(uint8_t), file_size, savefp);
			fclose(savefp);
		}
	}
	else
	{
		file_size = GPRAM_Mask ? (GPRAM_Mask + 1) : 0;
		if (file_size > 0)
		{
			savefp = fopen(path, "wb");
			if (savefp)
			{
				fwrite(GPRAM, sizeof(uint8_t), file_size, savefp);
				fclose(savefp);
			}
		}
	}
}

/* Main entrypoint of the emulator */
int main(int argc, char* argv[])
{
	int isloaded;
	
	printf("Starting VB-BoyEmu\n");
    
	if (argc < 2)
	{
		printf("Specify a ROM to load in memory\n");
		return 0;
	}
	
	snprintf(GameName_emu, sizeof(GameName_emu), "%s", basename(argv[1]));

	Init_Video();
	Audio_Init();
	
	isloaded = Load_Game_Memory(argv[1]);
	if (!isloaded)
	{
		printf("Could not load ROM in memory\n");
		return 0;
	}
	
	Init_Configuration();
	
    // get the game ready
    while (!exit_vb)
    {
		switch(emulator_state)
		{
			case 0:
				Emulation_Run();
			break;
			case 1:
				Menu();
			break;
		}
    }
    
	Clean();
    Audio_Close();
    Video_Close();

    return 0;
}
