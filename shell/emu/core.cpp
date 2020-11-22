#include <stdlib.h>
#include <stdio.h>
#define _BSD_SOURCE
#include <sys/time.h>
#include <stdarg.h>
#include <iconv.h>
#include "mednafen/mempatcher.h"
#include "mednafen/git.h"
#include "mednafen/state_helpers.h"
#include "mednafen/masmem.h"
#include "mednafen/settings.h"

#include "shared.h"
#include "video_blit.h"
#include "sound_output.h"
#include "menu.h"
#include "input_emu.h"

/* Forward declarations */
void MDFN_LoadGameCheats(void *override);
void MDFN_FlushGameCheats(int nosave);

static bool overscan;
static double last_sound_rate;
static struct MDFN_PixelFormat last_pixel_format;

static struct MDFN_Surface surf;

char GameName_emu[512];
extern uint32_t emulator_state;

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
#include "mednafen/mempatcher.h"
#include "mednafen/hw_cpu/v810/v810_cpu.h"

#include "games_database_patch.h"

enum
{
 ANAGLYPH_PRESET_DISABLED = 0,
 ANAGLYPH_PRESET_RED_BLUE,
 ANAGLYPH_PRESET_RED_CYAN,
 ANAGLYPH_PRESET_RED_ELECTRICCYAN,
 ANAGLYPH_PRESET_RED_GREEN,
 ANAGLYPH_PRESET_GREEN_MAGENTA,
 ANAGLYPH_PRESET_YELLOW_BLUE,
};

static const uint32 AnaglyphPreset_Colors[][2] =
{
 { 0, 0 },
 { 0xFF0000, 0x0000FF },
 { 0xFF0000, 0x00B7EB },
 { 0xFF0000, 0x00FFFF },
 { 0xFF0000, 0x00FF00 },
 { 0x00FF00, 0xFF00FF },
 { 0xFFFF00, 0x0000FF },
};

#define STICK_DEADZONE 0x4000
#define RIGHT_DPAD_LEFT 0x1000
#define RIGHT_DPAD_RIGHT 0x0020
#define RIGHT_DPAD_UP 0x0010
#define RIGHT_DPAD_DOWN 0x2000

static uint32 VB3DMode;

static Blip_Buffer sbuf[2];

static uint8_t *WRAM = NULL;

static uint8_t *GPRAM = NULL;
static uint32 GPRAM_Mask;

static uint8_t *GPROM = NULL;
static uint32 GPROM_Mask;

V810 *VB_V810 = NULL;

VSU *VB_VSU = NULL;
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
	256,   // Framebuffer height
	2,     // Number of output sound channels
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

   VB_V810->SetInt(ilevel);
}

extern "C" void VBIRQ_Assert(int source, bool assert)
{
   /*assert(source >= 0 && source <= 4);*/

   IRQ_Asserted &= ~(1 << source);

   if(assert)
      IRQ_Asserted |= 1 << source;

   RecalcIntLevel();
}

static uint8_t HWCTRL_Read(v810_timestamp_t &timestamp, uint32 A)
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

static void HWCTRL_Write(v810_timestamp_t &timestamp, uint32 A, uint8_t V)
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

uint8_t MDFN_FASTCALL MemRead8(v810_timestamp_t &timestamp, uint32 A)
{
   uint8_t ret = 0;
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

uint16 MDFN_FASTCALL MemRead16(v810_timestamp_t &timestamp, uint32 A)
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

void MDFN_FASTCALL MemWrite8(v810_timestamp_t &timestamp, uint32 A, uint8_t V)
{
 A &= (1 << 27) - 1;

 //if((A >> 24) <= 2)
 // printf("Write8: %d %08x %02x\n", timestamp, A, V);

 switch(A >> 24)
 {
  case 0: VIP_Write8(timestamp, A, V);
          break;

  case 1: VB_VSU->Write((timestamp + VSU_CycleFix) >> 2, A, V);
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

void MDFN_FASTCALL MemWrite16(v810_timestamp_t &timestamp, uint32 A, uint16 V)
{
 A &= (1 << 27) - 1;

 //if((A >> 24) <= 2)
 // printf("Write16: %d %08x %04x\n", timestamp, A, V);

 switch(A >> 24)
 {
  case 0: VIP_Write16(timestamp, A, V);
          break;

  case 1: VB_VSU->Write((timestamp + VSU_CycleFix) >> 2, A, V);
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

extern "C" void VB_SetEvent(const int type, const v810_timestamp_t next_timestamp)
{
   //assert(next_timestamp > VB_V810->v810_timestamp);

	if(type == VB_EVENT_VIP)
		next_vip_ts = next_timestamp;
	else if(type == VB_EVENT_TIMER)
		next_timer_ts = next_timestamp;
	else if(type == VB_EVENT_INPUT)
		next_input_ts = next_timestamp;

	if(next_timestamp < VB_V810->GetEventNT())
		VB_V810->SetEventNT(next_timestamp);
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

	VB_V810->SetEventNT(CalcNextTS());
	//printf("FEU: %d %d %d\n", next_vip_ts, next_timer_ts, next_input_ts);
}

static void VB_Power(void)
{
	memset(WRAM, 0, 65536);

	VIP_Power();
	VB_VSU->Power();
	TIMER_Power();
	VBINPUT_Power();

	EventReset();
	IRQ_Asserted = 0;
	RecalcIntLevel();
	VB_V810->Reset();

	VSU_CycleFix = 0;
	WCR = 0;

	ForceEventUpdates(0);  //VB_V810->v810_timestamp);
}

static void SettingChanged(const char *name)
{
   if(!strcmp(name, "vb.3dmode"))
   {
      VB3DMode = MDFN_GetSettingUI("vb.3dmode");
      uint32 prescale = MDFN_GetSettingUI("vb.liprescale");
      uint32 sbs_separation = MDFN_GetSettingUI("vb.sidebyside.separation");

      VIP_Set3DMode(VB3DMode, MDFN_GetSettingUI("vb.3dreverse"), prescale, sbs_separation);
   }
   else if(!strcmp(name, "vb.disable_parallax"))
   {
      VIP_SetParallaxDisable(MDFN_GetSettingB("vb.disable_parallax"));
   }
   else if(!strcmp(name, "vb.anaglyph.lcolor") || !strcmp(name, "vb.anaglyph.rcolor") ||
         !strcmp(name, "vb.anaglyph.preset") || !strcmp(name, "vb.default_color"))
   {
      uint32 lcolor = MDFN_GetSettingUI("vb.anaglyph.lcolor"), rcolor = MDFN_GetSettingUI("vb.anaglyph.rcolor");
      int preset = MDFN_GetSettingI("vb.anaglyph.preset");

      if(preset != ANAGLYPH_PRESET_DISABLED)
      {
         lcolor = AnaglyphPreset_Colors[preset][0];
         rcolor = AnaglyphPreset_Colors[preset][1];
      }
      VIP_SetAnaglyphColors(lcolor, rcolor);
      VIP_SetDefaultColor(MDFN_GetSettingUI("vb.default_color"));
   }
   else if(!strcmp(name, "vb.input.instant_read_hack"))
   {
      VBINPUT_SetInstantReadHack(MDFN_GetSettingB("vb.input.instant_read_hack"));
   }
   else if(!strcmp(name, "vb.instant_display_hack"))
      VIP_SetInstantDisplayHack(MDFN_GetSettingB("vb.instant_display_hack"));
   else if(!strcmp(name, "vb.allow_draw_skip"))
      VIP_SetAllowDrawSkip(MDFN_GetSettingB("vb.allow_draw_skip"));
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

static int Load(const uint8_t *data, size_t size)
{
   V810_Emu_Mode cpu_mode = (V810_Emu_Mode)MDFN_GetSettingI("vb.cpu_emulation");

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

   printf("V810 Emulation Mode: %s\n", (cpu_mode == V810_EMU_MODE_ACCURATE) ? "Accurate" : "Fast");

   VB_V810 = new V810();
   VB_V810->Init(cpu_mode, true);

   VB_V810->SetMemReadHandlers(MemRead8, MemRead16, NULL);
   VB_V810->SetMemWriteHandlers(MemWrite8, MemWrite16, NULL);

   VB_V810->SetIOReadHandlers(MemRead8, MemRead16, NULL);
   VB_V810->SetIOWriteHandlers(MemWrite8, MemWrite16, NULL);

   for(int i = 0; i < 256; i++)
   {
      VB_V810->SetMemReadBus32(i, false);
      VB_V810->SetMemWriteBus32(i, false);
   }

   std::vector<uint32> Map_Addresses;

   for(uint64 A = 0; A < 1ULL << 32; A += (1 << 27))
   {
      for(uint64 sub_A = 5 << 24; sub_A < (6 << 24); sub_A += 65536)
      {
         Map_Addresses.push_back(A + sub_A);
      }
   }

   WRAM = VB_V810->SetFastMap(&Map_Addresses[0], 65536, Map_Addresses.size(), "WRAM");
   Map_Addresses.clear();


   // Round up the ROM size to 65536(we mirror it a little later)
   GPROM_Mask = (size < 65536) ? (65536 - 1) : (size - 1);

   for(uint64 A = 0; A < 1ULL << 32; A += (1 << 27))
   {
      for(uint64 sub_A = 7 << 24; sub_A < (8 << 24); sub_A += GPROM_Mask + 1)
      {
         Map_Addresses.push_back(A + sub_A);
         //printf("%08x\n", (uint32)(A + sub_A));
      }
   }


   GPROM = VB_V810->SetFastMap(&Map_Addresses[0], GPROM_Mask + 1, Map_Addresses.size(), "Cart ROM");
   Map_Addresses.clear();

   // Mirror ROM images < 64KiB to 64KiB
   for(uint64 i = 0; i < 65536; i += size)
      memcpy(GPROM + i, data, size);

   GPRAM_Mask = 0xFFFF;

   for(uint64 A = 0; A < 1ULL << 32; A += (1 << 27))
   {
      for(uint64 sub_A = 6 << 24; sub_A < (7 << 24); sub_A += GPRAM_Mask + 1)
      {
         //printf("GPRAM: %08x\n", A + sub_A);
         Map_Addresses.push_back(A + sub_A);
      }
   }


   GPRAM = VB_V810->SetFastMap(&Map_Addresses[0], GPRAM_Mask + 1, Map_Addresses.size(), "Cart RAM");
   Map_Addresses.clear();

   memset(GPRAM, 0, GPRAM_Mask + 1);

   VIP_Init();
   VB_VSU = new VSU(&sbuf[0], &sbuf[1]);
   VBINPUT_Init();

   VB3DMode = MDFN_GetSettingUI("vb.3dmode");
   uint32 prescale = MDFN_GetSettingUI("vb.liprescale");
   uint32 sbs_separation = MDFN_GetSettingUI("vb.sidebyside.separation");

   VIP_Set3DMode(VB3DMode, MDFN_GetSettingUI("vb.3dreverse"), prescale, sbs_separation);


   SettingChanged("vb.3dmode");
   SettingChanged("vb.disable_parallax");
   SettingChanged("vb.anaglyph.lcolor");
   SettingChanged("vb.anaglyph.rcolor");
   SettingChanged("vb.anaglyph.preset");
   SettingChanged("vb.default_color");

   SettingChanged("vb.instant_display_hack");
   SettingChanged("vb.allow_draw_skip");

   SettingChanged("vb.input.instant_read_hack");

   MDFNGameInfo->fps = (int64)20000000 * 65536 * 256 / (259 * 384 * 4);


   VB_Power();

   MDFNGameInfo->nominal_width = 384;
   MDFNGameInfo->nominal_height = 224;
   MDFNGameInfo->fb_width = 384;
   MDFNGameInfo->fb_height = 224;

   switch(VB3DMode)
   {
      default: break;

      case VB3DMODE_VLI:
               MDFNGameInfo->nominal_width = 768 * prescale;
               MDFNGameInfo->nominal_height = 224;
               MDFNGameInfo->fb_width = 768 * prescale;
               MDFNGameInfo->fb_height = 224;
               break;

      case VB3DMODE_HLI:
               MDFNGameInfo->nominal_width = 384;
               MDFNGameInfo->nominal_height = 448 * prescale;
               MDFNGameInfo->fb_width = 384;
               MDFNGameInfo->fb_height = 448 * prescale;
               break;

      case VB3DMODE_CSCOPE:
               MDFNGameInfo->nominal_width = 512;
               MDFNGameInfo->nominal_height = 384;
               MDFNGameInfo->fb_width = 512;
               MDFNGameInfo->fb_height = 384;
               break;

      case VB3DMODE_SIDEBYSIDE:
               MDFNGameInfo->nominal_width = 384 * 2 + sbs_separation;
               MDFNGameInfo->nominal_height = 224;
               MDFNGameInfo->fb_width = 384 * 2 + sbs_separation;
               MDFNGameInfo->fb_height = 224;
               break;
   }
   MDFNGameInfo->lcm_width = MDFNGameInfo->fb_width;
   MDFNGameInfo->lcm_height = MDFNGameInfo->fb_height;


   MDFNMP_Init(32768, ((uint64)1 << 27) / 32768);
   MDFNMP_AddRAM(65536, 5 << 24, WRAM);
   if((GPRAM_Mask + 1) >= 32768)
      MDFNMP_AddRAM(GPRAM_Mask + 1, 6 << 24, GPRAM);
   return(1);
}

static void CloseGame(void)
{
   //VIP_Kill();

   if(VB_VSU)
   {
      delete VB_VSU;
      VB_VSU = NULL;
   }

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

   if(VB_V810)
   {
      VB_V810->Kill();
      delete VB_V810;
      VB_V810 = NULL;
   }
}

extern "C" void VB_ExitLoop(void)
{
   VB_V810->Exit();
}

static void Emulate(EmulateSpecStruct *espec, int16_t *sound_buf)
{
	v810_timestamp_t v810_timestamp;

	MDFNMP_ApplyPeriodicCheats();

	VBINPUT_Frame();

	if(espec->SoundFormatChanged)
	{
		for(int y = 0; y < 2; y++)
		{
			Blip_Buffer_set_sample_rate(&sbuf[y], espec->SoundRate ? espec->SoundRate : 44100, 50);
			Blip_Buffer_set_clock_rate(&sbuf[y], (long)(VB_MASTER_CLOCK / 4));
			Blip_Buffer_bass_freq(&sbuf[y], 20);
		}
	}

	VIP_StartFrame(espec);

	v810_timestamp = VB_V810->Run(EventHandler);

	FixNonEvents();
	ForceEventUpdates(v810_timestamp);

	VB_VSU->EndFrame((v810_timestamp + VSU_CycleFix) >> 2);

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

   VB_V810->ResetTS(0);
}

extern "C" int StateAction(StateMem *sm, int load, int data_only)
{
   const v810_timestamp_t timestamp = VB_V810->v810_timestamp;
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

   ret &= VB_V810->StateAction(sm, load, data_only);

   ret &= VB_VSU->StateAction(sm, load, data_only);
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

static void SetLayerEnableMask(uint64 mask) { }

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

static const InputDeviceInputInfoStruct IDII[] =
{
 { "a", "A", 7, IDIT_BUTTON_CAN_RAPID,  NULL },
 { "b", "B", 6, IDIT_BUTTON_CAN_RAPID, NULL },
 { "rt", "Right-Back", 13, IDIT_BUTTON, NULL },
 { "lt", "Left-Back", 12, IDIT_BUTTON, NULL },

 { "up-r", "UP ↑ (Right D-Pad)", 8, IDIT_BUTTON, "down-r" },
 { "right-r", "RIGHT → (Right D-Pad)", 11, IDIT_BUTTON, "left-r" },

 { "right-l", "RIGHT → (Left D-Pad)", 3, IDIT_BUTTON, "left-l" },
 { "left-l", "LEFT ← (Left D-Pad)", 2, IDIT_BUTTON, "right-l" },
 { "down-l", "DOWN ↓ (Left D-Pad)", 1, IDIT_BUTTON, "up-l" },
 { "up-l", "UP ↑ (Left D-Pad)", 0, IDIT_BUTTON, "down-l" },

 { "start", "Start", 5, IDIT_BUTTON, NULL },
 { "select", "Select", 4, IDIT_BUTTON, NULL },

 { "left-r", "LEFT ← (Right D-Pad)", 10, IDIT_BUTTON, "right-r" },
 { "down-r", "DOWN ↓ (Right D-Pad)", 9, IDIT_BUTTON, "up-r" },
};

static InputDeviceInfoStruct InputDeviceInfo[] =
{
 {
  "gamepad",
  "Gamepad",
  NULL,
  NULL,
  sizeof(IDII) / sizeof(InputDeviceInputInfoStruct),
  IDII,
 }
};

static const InputPortInfoStruct PortInfo[] =
{
 { "builtin", "Built-In", sizeof(InputDeviceInfo) / sizeof(InputDeviceInfoStruct), InputDeviceInfo, "gamepad" }
};

static InputInfoStruct InputInfo =
{
 sizeof(PortInfo) / sizeof(InputPortInfoStruct),
 PortInfo
};

static bool MDFNI_LoadGame(const uint8_t *data, size_t size)
{
   MDFNGameInfo = &EmulatedVB;

   if(Load(data, size) <= 0)
      goto error;

   MDFN_LoadGameCheats(NULL);
   MDFNMP_InstallReadPatches();

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

   MDFN_FlushGameCheats(0);

   CloseGame();

   MDFNMP_Kill();

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
	setting_vb_3dmode = VB3DMODE_ANAGLYPH;
	setting_vb_anaglyph_preset = 0;
	
	/* Red and black */
	setting_vb_lcolor = 0xAA0000;
	setting_vb_rcolor = 0x000000;
	setting_vb_cpu_emulation = V810_EMU_MODE_FAST;
	setting_vb_right_analog_to_digital = true;
	setting_vb_right_invert_x = false;
	setting_vb_right_invert_y = false;
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

	overscan = false;
	check_variables();

	fp = fopen(game_name, "rb");
	if (!fp) return 0;
	fseek (fp, 0, SEEK_END);   // non-portable
	length = ftell(fp);
	rom_data = (uint8_t *)malloc(length);
	fseek (fp, 0, SEEK_SET);
	fread (rom_data, sizeof(uint8_t), length, fp);
	fclose (fp);
	
	printf("Rom size %lu\n", length);

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
	last_pixel_format.bpp        = 0;
	last_pixel_format.colorspace = 0;
	last_pixel_format.Rshift     = 0;
	last_pixel_format.Gshift     = 0;
	last_pixel_format.Bshift     = 0;
	last_pixel_format.Ashift     = 0;
#elif defined(WANT_8BPP)
	extern SDL_Surface* sdl_screen;
	pix_fmt.bpp        = 8;
	pix_fmt.Rshift     = sdl_screen->format->Rshift;
	pix_fmt.Gshift     = sdl_screen->format->Gshift;
	pix_fmt.Bshift     = sdl_screen->format->Bshift;
	pix_fmt.Ashift     = sdl_screen->format->Ashift;
	last_pixel_format.bpp        = 0;
	last_pixel_format.colorspace = 0;
	last_pixel_format.Rshift     = 0;
	last_pixel_format.Gshift     = 0;
	last_pixel_format.Bshift     = 0;
	last_pixel_format.Ashift     = 0;
#else
	pix_fmt.bpp        = 32;
	pix_fmt.Rshift     = 16;
	pix_fmt.Gshift     = 8;
	pix_fmt.Bshift     = 0;
	pix_fmt.Ashift     = 24;
	last_pixel_format.bpp        = 0;
	last_pixel_format.colorspace = 0;
	last_pixel_format.Rshift     = 0;
	last_pixel_format.Gshift     = 0;
	last_pixel_format.Bshift     = 0;
	last_pixel_format.Ashift     = 0;
#endif
	pix_fmt.colorspace = MDFN_COLORSPACE_RGB;

	surf.format                  = pix_fmt;
	surf.pixels16                = NULL;
	surf.pixels                  = NULL;
#if defined(WANT_8BPP)
	surf.palette = NULL;
#endif
	

#if defined(WANT_8BPP)
	surf.pixels8 = (uint8 *)internal_pix;
	surf.palette = (MDFN_PaletteEntry*)calloc(sizeof(MDFN_PaletteEntry), 256);
#elif defined(WANT_16BPP)
	surf.pixels16                = (uint16 *)internal_pix;
#elif defined(WANT_32BPP)
	surf.pixels                  = (uint32 *)internal_pix;
#elif defined(WANT_8BPP)
   surf.pixels8 = NULL;
   if(surf.palette)
      free(surf.palette);
   surf.palette = NULL;
#endif
	surf.w                       = FB_WIDTH;
	surf.h                       = FB_HEIGHT;
	surf.pitchinpix              = FB_WIDTH;
	hookup_ports(true);
	check_variables();
	return true;
}

void retro_unload_game(void)
{
   MDFNI_CloseGame();
}

static void update_input(void)
{
   /*unsigned i,j;
   int16_t joy_bits[MAX_PLAYERS] = {0};*/

	input_buf[0] = Read_Input();
   /*static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_L2, //right d-pad UP
      RETRO_DEVICE_ID_JOYPAD_R3, //right d-pad RIGHT
      RETRO_DEVICE_ID_JOYPAD_RIGHT, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_LEFT, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_DOWN, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_UP, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RETRO_DEVICE_ID_JOYPAD_R2, //right d-pad LEFT
      RETRO_DEVICE_ID_JOYPAD_L3, //right d-pad DOWN
   };

   for (j = 0; j < MAX_PLAYERS; j++)
   {
      if (libretro_supports_bitmasks)
         joy_bits[j] = input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
      else
      {
         for (i = 0; i < (RETRO_DEVICE_ID_JOYPAD_R3+1); i++)
            joy_bits[j] |= input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, i) ? (1 << i) : 0;
      }
   }

   for (j = 0; j < MAX_PLAYERS; j++)
   {
      for (i = 0; i < MAX_BUTTONS; i++)
         input_buf[j] |= (map[i] != -1u) && (joy_bits[j] & (1 << map[i])) ? (1 << i) : 0;

      if (setting_vb_right_analog_to_digital) {
         int16_t analog_x = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
         int16_t analog_y = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);

         if (abs(analog_x) > STICK_DEADZONE)
            input_buf[j] |= (analog_x < 0) ^ !setting_vb_right_invert_x ? RIGHT_DPAD_RIGHT : RIGHT_DPAD_LEFT;
         if (abs(analog_y) > STICK_DEADZONE)
            input_buf[j] |= (analog_y < 0) ^ !setting_vb_right_invert_y ? RIGHT_DPAD_DOWN : RIGHT_DPAD_UP;
      }

#ifdef MSB_FIRST
      union {
         uint8_t b[2];
         uint16_t s;
      } u;
      u.s = input_buf[j];
      input_buf[j] = u.b[0] | u.b[1] << 8;
#endif
   }*/
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
static uint32_t SkipCnt = 0, video_frames = 0, FPS = MEDNAFEN_CORE_TIMING_FPS, FrameSkip;
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
   //input_poll_cb();

   update_input();

   static int16_t sound_buf[0x10000];
   static MDFN_Rect rects[FB_MAX_HEIGHT];
   bool resolution_changed = false;
   rects[0].w = ~0;

   EmulateSpecStruct spec = {0};
   spec.surface    = &surf;
   spec.SoundRate  = SOUND_OUTPUT_FREQUENCY;
   spec.LineWidths = rects;
   spec.SoundBufMaxSize = sizeof(sound_buf) / 2;
   spec.SoundBufSize = 0;
   spec.VideoFormatChanged = false;
   spec.SoundFormatChanged = false;
	SkipCnt++;
	if (SkipCnt > 4) SkipCnt = 0;
   spec.skip = TblSkip[FrameSkip][SkipCnt];

   if (memcmp(&last_pixel_format, &spec.surface->format, sizeof(struct MDFN_PixelFormat)))
   {
      spec.VideoFormatChanged = true;

      last_pixel_format = spec.surface->format;
   }

   if (spec.SoundRate != last_sound_rate)
   {
      spec.SoundFormatChanged = true;
      last_sound_rate = spec.SoundRate;
   }

   Emulate(&spec, sound_buf);

   int16 *const SoundBuf = sound_buf + spec.SoundBufSizeALMS * EmulatedVB.soundchan;
   int32 SoundBufSize = spec.SoundBufSize - spec.SoundBufSizeALMS;
   const int32 SoundBufMaxSize = spec.SoundBufMaxSize - spec.SoundBufSizeALMS;

   spec.SoundBufSize = spec.SoundBufSizeALMS + SoundBufSize;
   
#ifdef FRAMESKIP
	if (spec.skip == false) video_frames++;
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
	Update_Video_Ingame();

   video_frames++;
   audio_frames += spec.SoundBufSize;

   //audio_batch_cb(sound_buf, spec.SoundBufSize);
	Audio_Write((int16_t*) sound_buf, spec.SoundBufSize);
}


void Clean(void)
{
   surf.pixels8    = NULL;
   surf.pixels16   = NULL;
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

void retro_cheat_reset(void) { }
void retro_cheat_set(unsigned, bool, const char *) { }

void MDFND_DispMessage(unsigned char *str)
{
}

void MDFND_MidSync(const EmulateSpecStruct *) { }

void MDFN_MidLineUpdate(EmulateSpecStruct *espec, int y)
{
#if 0
   MDFND_MidLineUpdate(espec, y);
#endif
}

void MDFND_PrintError(const char* err)
{
}

void SaveState(char* path, uint_fast8_t state)
{	
	FILE* savefp;
	size_t file_size;
	char* buffer;
	if (state == 1)
	{
		savefp = fopen(path, "rb");
		if (savefp)
		{
			fseek(savefp, 0, SEEK_END);
			file_size = ftell(savefp);
			fseek(savefp, 0, SEEK_SET);
			fread(&buffer, sizeof(uint8_t), file_size, savefp);
			buffer = (char*)malloc(file_size);
			retro_unserialize(buffer, file_size);
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
			fwrite(&buffer, sizeof(uint8_t), file_size, savefp);
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
		/* Currently not working for some reasons */
		/*savefp = fopen(path, "rb");
		if (savefp)
		{
			fseek(savefp, 0, SEEK_END);
			file_size = ftell(savefp);
			fseek(savefp, 0, SEEK_SET);
			fread(&GPRAM, sizeof(uint8_t), file_size, savefp);
			fclose(savefp);
		}*/
	}
	else
	{
		file_size = GPRAM_Mask ? (GPRAM_Mask + 1) : 0;
		if (file_size > 0)
		{
			savefp = fopen(path, "wb");
			if (savefp)
			{
				fwrite(&GPRAM, sizeof(uint8_t), file_size, savefp);
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
