/* V810 Emulator
 *
 * Copyright (C) 2006 David Tucker
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

/* Alternatively, the V810 emulator code(and all V810 emulation header files) can be used/distributed under the following license(you can adopt either
   license exclusively for your changes by removing one of these license headers, but it's STRONGLY preferable
   to keep your changes dual-licensed as well):

This Reality Boy emulator is copyright (C) David Tucker 1997-2008, all rights
reserved.   You may use this code as long as you make no money from the use of
this code and you acknowledge the original author (Me).  I reserve the right to
dictate who can use this code and how (Just so you don't do something stupid
with it).
   Most Importantly, this code is swap ware.  If you use It send along your new
program (with code) or some other interesting tidbits you wrote, that I might be
interested in.
   This code is in beta, there are bugs!  I am not responsible for any damage
done to your computer, reputation, ego, dog, or family life due to the use of
this code.  All source is provided as is, I make no guaranties, and am not
responsible for anything you do with the code (legal or otherwise).
   Virtual Boy is a trademark of Nintendo, and V810 is a trademark of NEC.  I am
in no way affiliated with either party and all information contained hear was
found freely through public domain sources.
*/

//////////////////////////////////////////////////////////
// CPU routines
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <boolean.h>

#include "../../mednafen-types.h"

#include "../../masmem.h"
#include "../../math_ops.h"
#include "../../state_helpers.h"

#include "v810_opt.h"
#include "v810_cpu.h"

#define min(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a < _b ? _a : _b; })
#define max(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })

// Make sure P_REG[] is the first variable/array in this class, so non-zerfo offset encoding(at assembly level) isn't necessary to access it.
static uint32 P_REG[32];  // Program registers pr0-pr31
static uint32 S_REG[32];  // System registers sr0-sr31
static uint8 *PC_ptr;
static uint8 *PC_base;
static uint32 IPendingCache;
static void RecalcIPendingCache(void);

static v810_timestamp_t next_event_ts = 0x7FFFFFFF;
v810_timestamp_t v810_timestamp = 0;

extern uint8 MDFN_FASTCALL MemRead8(v810_timestamp_t timestamp, uint32 A);
extern uint16 MDFN_FASTCALL MemRead16(v810_timestamp_t timestamp, uint32 A);
extern void MDFN_FASTCALL MemWrite8(v810_timestamp_t timestamp, uint32 A, uint8_t V);
extern void MDFN_FASTCALL MemWrite16(v810_timestamp_t timestamp, uint32 A, uint16 V);

/*uint8 MDFN_FASTCALL (*MemRead8)(v810_timestamp_t timestamp, uint32 A);
uint16 MDFN_FASTCALL (*MemRead16)(v810_timestamp_t timestamp, uint32 A);*/
uint32 MDFN_FASTCALL (*MemRead32)(v810_timestamp_t timestamp, uint32 A);

/*void MDFN_FASTCALL (*MemWrite8)(v810_timestamp_t timestamp, uint32 A, uint8 V);
void MDFN_FASTCALL (*MemWrite16)(v810_timestamp_t timestamp, uint32 A, uint16 V);
*/
void MDFN_FASTCALL (*MemWrite32)(v810_timestamp_t timestamp, uint32 A, uint32 V);

uint8 MDFN_FASTCALL (*IORead8)(v810_timestamp_t timestamp, uint32 A);
uint16 MDFN_FASTCALL (*IORead16)(v810_timestamp_t timestamp, uint32 A);
uint32 MDFN_FASTCALL (*IORead32)(v810_timestamp_t timestamp, uint32 A);

void MDFN_FASTCALL (*IOWrite8)(v810_timestamp_t timestamp, uint32 A, uint8 V);
void MDFN_FASTCALL (*IOWrite16)(v810_timestamp_t timestamp, uint32 A, uint16 V);

INLINE void V810_SetFlag(uint32 n, bool condition);
INLINE void V810_SetSZ(uint32 value);

void MDFN_FASTCALL (*IOWrite32)(v810_timestamp_t timestamp, uint32 A, uint32 V);

static bool MemReadBus32[256];      // Corresponding to the upper 8 bits of the memory address map.
static bool MemWriteBus32[256];

static int32 lastop;    // Set to -1 on FP/MUL/DIV, 0x100 on LD, 0x200 on ST, 0x400 on in, 0x800 on out, and the actual opcode * 2(or >= 0) on everything else.

#define LASTOP_LD       0x100
#define LASTOP_ST       0x200
#define LASTOP_IN       0x400
#define LASTOP_OUT      0x800

enum
{
	HALT_NONE = 0,
	HALT_HALT = 1,
	HALT_FATAL_EXCEPTION = 2
};

static uint8 Halted;
static bool Running;
static int ilevel;
static bool in_bstr;
static uint16 in_bstr_to;

bool bstr_subop(v810_timestamp_t timestamp, int sub_op, int arg1);
void fpu_subop(v810_timestamp_t timestamp, int sub_op, int arg1, int arg2);
void V810_Exception(uint32 handler, uint16 eCode);

// Caching-related:
typedef struct
{
	uint32 tag;
	uint32 data[2];
	bool data_valid[2];
} V810_CacheEntry_t;
V810_CacheEntry_t Cache[128];

// Bitstring variables.
static uint32 src_cache;
static uint32 dst_cache;
static bool have_src_cache, have_dst_cache;

static uint8 *FastMap[(1ULL << 32) / V810_FAST_MAP_PSIZE];
static uint8* FastMapAllocList;

// For CacheDump and CacheRestore
void V810_CacheOpMemStore(v810_timestamp_t timestamp, uint32 A, uint32 V);
uint32 V810_CacheOpMemLoad(v810_timestamp_t timestamp, uint32 A);

void V810_CacheClear(v810_timestamp_t timestamp, uint32 start, uint32 count);
void V810_CacheDump(v810_timestamp_t timestamp, const uint32 SA);
void V810_CacheRestore(v810_timestamp_t timestamp, const uint32 SA);

uint32 V810_RDCACHE(v810_timestamp_t timestamp, uint32 addr);
//
// End caching related
//

uint16 V810_RDOP(v810_timestamp_t timestamp, uint32 addr, uint32 meow);
void V810_SetFlag(uint32 n, bool condition);
void V810_SetSZ(uint32 value);
void V810_SetSREG(v810_timestamp_t timestamp, unsigned int which, uint32 value);
uint32 V810_GetSREG(unsigned int which);
bool V810_IsSubnormal(uint32 fpval);
void V810_FPU_Math_Template(float32 (*func)(float32, float32), uint32 arg1, uint32 arg2);
void V810_FPU_DoException(void);
bool V810_CheckFPInputException(uint32 fpval);
bool V810_FPU_DoesExceptionKillResult(void);
void V810_SetFPUOPNonFPUFlags(uint32 result);

uint32 V810_BSTR_RWORD(v810_timestamp_t timestamp, uint32 A);
void V810_BSTR_WWORD(v810_timestamp_t timestamp, uint32 A, uint32 V);
bool V810_Do_BSTR_Search(v810_timestamp_t timestamp, const int inc_mul, unsigned int bit_test);
uint8 DummyRegion[V810_FAST_MAP_PSIZE + V810_FAST_MAP_TRAMPOLINE_SIZE];

/* extern only */
INLINE void V810_ResetTS(v810_timestamp_t new_base_timestamp)
{
	next_event_ts -= (v810_timestamp - new_base_timestamp);
	v810_timestamp = new_base_timestamp;
}

INLINE void V810_SetEventNT(const v810_timestamp_t timestamp)
{
	next_event_ts = timestamp;
}

INLINE v810_timestamp_t V810_GetEventNT(void)
{
	return(next_event_ts);
}

/*
V810_V810()
{
 MemRead8 = NULL;
 MemRead16 = NULL;
 MemRead32 = NULL;

 IORead8 = NULL;
 IORead16 = NULL;
 IORead32 = NULL;

 MemWrite8 = NULL;
 MemWrite16 = NULL;
 MemWrite32 = NULL;

 IOWrite8 = NULL;
 IOWrite16 = NULL;
 IOWrite32 = NULL;

 memset(FastMap, 0, sizeof(FastMap));

 memset(MemReadBus32, 0, sizeof(MemReadBus32));
 memset(MemWriteBus32, 0, sizeof(MemWriteBus32));

 v810_timestamp = 0;
 next_event_ts = 0x7FFFFFFF;
}
*/

static INLINE void RecalcIPendingCache(void)
{
	IPendingCache = 0;

	// Of course don't generate an interrupt if there's not one pending!
	if(ilevel < 0)
		return;

	// If CPU is halted because of a fatal exception, don't let an interrupt
	// take us out of this halted status.
	if(Halted == HALT_FATAL_EXCEPTION) 
		return;

	// If the NMI pending, exception pending, and/or interrupt disabled bit
	// is set, don't accept any interrupts.
	if(S_REG[PSW] & (PSW_NP | PSW_EP | PSW_ID))
		return;

	// If the interrupt level is lower than the interrupt enable level, don't
	// accept it.
	if(ilevel < (int)((S_REG[PSW] & PSW_IA) >> 16))
		return;

	IPendingCache = 0xFF;
}


// TODO: "An interrupt that occurs during restore/dump/clear operation is internally held and is accepted after the
// operation in progress is finished. The maskable interrupt is held internally only when the EP, NP, and ID flags
// of PSW are all 0."
//
// This behavior probably doesn't have any relevance on the PC-FX, unless we're sadistic
// and try to restore cache from an interrupt acknowledge register or dump it to a register
// controlling interrupt masks...  I wanna be sadistic~

void V810_CacheClear(v810_timestamp_t timestamp, uint32 start, uint32 count)
{
	//printf("Cache clear: %08x %08x\n", start, count);
	for(uint32 i = 0; i < count && (i + start) < 128; i++)
		memset(&Cache[i + start], 0, sizeof(V810_CacheEntry_t));
}

INLINE void V810_CacheOpMemStore(v810_timestamp_t timestamp, uint32 A, uint32 V)
{
	if(MemWriteBus32[A >> 24])
	{
		timestamp += 2;
		MemWrite32(timestamp, A, V);
	}
	else
	{
		timestamp += 2;
		MemWrite16(timestamp, A, V & 0xFFFF);

		timestamp += 2;
		MemWrite16(timestamp, A | 2, V >> 16);
	}
}

INLINE uint32 V810_CacheOpMemLoad(v810_timestamp_t timestamp, uint32 A)
{
	if(MemReadBus32[A >> 24])
	{
		timestamp += 2;
		return(MemRead32(timestamp, A));
	}
	else
	{
		uint32 ret;

		timestamp += 2;
		ret = MemRead16(timestamp, A);

		timestamp += 2;
		ret |= MemRead16(timestamp, A | 2) << 16;
		return(ret);
	}
}

void V810_CacheDump(v810_timestamp_t timestamp, const uint32 SA)
{
#if 0
 printf("Cache dump: %08x\n", SA);
#endif

 for(int i = 0; i < 128; i++)
 {
  V810_CacheOpMemStore(timestamp, SA + i * 8 + 0, Cache[i].data[0]);
  V810_CacheOpMemStore(timestamp, SA + i * 8 + 4, Cache[i].data[1]);
 }

 for(int i = 0; i < 128; i++)
 {
  uint32 icht = Cache[i].tag | ((int)Cache[i].data_valid[0] << 22) | ((int)Cache[i].data_valid[1] << 23);

  V810_CacheOpMemStore(timestamp, SA + 1024 + i * 4, icht);
 }

}

void V810_CacheRestore(v810_timestamp_t timestamp, const uint32 SA)
{
#if 0
 printf("Cache restore: %08x\n", SA);
#endif

 for(int i = 0; i < 128; i++)
 {
  Cache[i].data[0] = V810_CacheOpMemLoad(timestamp, SA + i * 8 + 0);
  Cache[i].data[1] = V810_CacheOpMemLoad(timestamp, SA + i * 8 + 4);
 }

 for(int i = 0; i < 128; i++)
 {
  uint32 icht;

  icht = V810_CacheOpMemLoad(timestamp, SA + 1024 + i * 4);

  Cache[i].tag = icht & ((1 << 22) - 1);
  Cache[i].data_valid[0] = (icht >> 22) & 1;
  Cache[i].data_valid[1] = (icht >> 23) & 1;
 }
}


INLINE uint32 V810_RDCACHE(v810_timestamp_t timestamp, uint32 addr)
{
 const int CI = (addr >> 3) & 0x7F;
 const int SBI = (addr & 4) >> 2;

 if(Cache[CI].tag == (addr >> 10))
 {
  if(!Cache[CI].data_valid[SBI])
  {
   timestamp += 2;       // or higher?  Penalty for cache miss seems to be higher than having cache disabled.
   if(MemReadBus32[addr >> 24])
    Cache[CI].data[SBI] = MemRead32(timestamp, addr & ~0x3);
   else
   {
    timestamp++;
    Cache[CI].data[SBI] = MemRead16(timestamp, addr & ~0x3) | ((MemRead16(timestamp, (addr & ~0x3) | 0x2) << 16));
   }
   Cache[CI].data_valid[SBI] = true;
  }
 }
 else
 {
  Cache[CI].tag = addr >> 10;

  timestamp += 2;	// or higher?  Penalty for cache miss seems to be higher than having cache disabled.
  if(MemReadBus32[addr >> 24])
   Cache[CI].data[SBI] = MemRead32(timestamp, addr & ~0x3);
  else
  {
   timestamp++;
   Cache[CI].data[SBI] = MemRead16(timestamp, addr & ~0x3) | ((MemRead16(timestamp, (addr & ~0x3) | 0x2) << 16));
  }
  //Cache[CI].data[SBI] = MemRead32(timestamp, addr & ~0x3);
  Cache[CI].data_valid[SBI] = true;
  Cache[CI].data_valid[SBI ^ 1] = false;
 }

 //{
 // // Caution: This can mess up DRAM page change penalty timings
 // uint32 dummy_timestamp = 0;
 // if(Cache[CI].data[SBI] != mem_rword(addr & ~0x3, dummy_timestamp))
 // {
 //  printf("Cache/Real Memory Mismatch: %08x %08x/%08x\n", addr & ~0x3, Cache[CI].data[SBI], mem_rword(addr & ~0x3, dummy_timestamp));
 // }
 //}

 return(Cache[CI].data[SBI]);
}

INLINE uint16 V810_RDOP(v810_timestamp_t timestamp, uint32 addr, uint32 meow)
{
	uint16 ret;
	if (!meow) meow = 2;
	
	if(S_REG[CHCW] & 0x2)
	{
		uint32 d32 = V810_RDCACHE(timestamp, addr);
		ret = d32 >> ((addr & 2) * 8);
	}
	else
	{
		timestamp += meow; //++;
		ret = MemRead16(timestamp, addr);
	}
	return(ret);
}

#define BRANCH_ALIGN_CHECK(x)	{ if((S_REG[CHCW] & 0x2) && (x & 0x2)) { ADDCLOCK(1); } }

// Reinitialize the defaults in the CPU
void V810_Reset() 
{
	memset(&Cache, 0, sizeof(Cache));

	memset(P_REG, 0, sizeof(P_REG));
	memset(S_REG, 0, sizeof(S_REG));
	memset(Cache, 0, sizeof(Cache));

	P_REG[0]      =  0x00000000;
	V810_SetPC(0xFFFFFFF0);

	S_REG[ECR]    =  0x0000FFF0;
	S_REG[PSW]    =  0x00008000;

	S_REG[PIR]	= 0x00005346;

	S_REG[TKCW]   =  0x000000E0;
	Halted = HALT_NONE;
	ilevel = -1;

	lastop = 0;

	in_bstr = false;

	RecalcIPendingCache();
}

bool V810_Init(void)
{
	in_bstr = false;
	in_bstr_to = 0;

	memset(DummyRegion, 0, V810_FAST_MAP_PSIZE);

	for(unsigned int i = V810_FAST_MAP_PSIZE; i < V810_FAST_MAP_PSIZE + V810_FAST_MAP_TRAMPOLINE_SIZE; i += 2)
	{
		DummyRegion[i + 0] = 0;
		DummyRegion[i + 1] = 0x36 << 2;
	}

	for(uint64 A = 0; A < (1ULL << 32); A += V810_FAST_MAP_PSIZE)
		FastMap[A / V810_FAST_MAP_PSIZE] = DummyRegion - A;

	return(true);
}

void V810_Kill(void)
{
	if (FastMapAllocList != NULL)
	{
		free(FastMapAllocList);
		FastMapAllocList = NULL;
	}
}

void V810_SetInt(int level)
{
 ilevel = level;
 RecalcIPendingCache();
}

uint8 *V810_SetFastMap(uint32 addresses[], uint32 length, unsigned int num_addresses, const char *name)
{
 uint8 *ret = NULL;

 if(!(ret = (uint8 *)malloc(length + V810_FAST_MAP_TRAMPOLINE_SIZE)))
  return(NULL);

 for(unsigned int i = length; i < length + V810_FAST_MAP_TRAMPOLINE_SIZE; i += 2)
 {
  ret[i + 0] = 0;
  ret[i + 1] = 0x36 << 2;
 }

 for(unsigned int i = 0; i < num_addresses; i++)
 {  
  for(uint64 addr = addresses[i]; addr != (uint64)addresses[i] + length; addr += V810_FAST_MAP_PSIZE)
  {
   //printf("%08x, %d, %s\n", addr, length, name);

   FastMap[addr / V810_FAST_MAP_PSIZE] = ret - addresses[i];
  }
 }

  FastMapAllocList = ret;
 //FastMapAllocList.push_back(ret);

 return(ret);
}


void V810_SetMemReadBus32(uint8 A, bool value)
{
 MemReadBus32[A] = value;
}

void V810_SetMemWriteBus32(uint8 A, bool value)
{
 MemWriteBus32[A] = value;
}

/*void V810_SetMemReadHandlers(uint8 MDFN_FASTCALL (*read8)(v810_timestamp_t , uint32), uint16 MDFN_FASTCALL (*read16)(v810_timestamp_t , uint32), uint32 MDFN_FASTCALL (*read32)(v810_timestamp_t , uint32))
{
 MemRead8 = read8;
 MemRead16 = read16;
 MemRead32 = read32;
}

void V810_SetMemWriteHandlers(void MDFN_FASTCALL (*write8)(v810_timestamp_t , uint32, uint8), void MDFN_FASTCALL (*write16)(v810_timestamp_t , uint32, uint16), void MDFN_FASTCALL (*write32)(v810_timestamp_t , uint32, uint32))
{
 MemWrite8 = write8;
 MemWrite16 = write16;
 MemWrite32 = write32;
}
*/

void V810_SetIOReadHandlers(uint8 MDFN_FASTCALL (*read8)(v810_timestamp_t , uint32), uint16 MDFN_FASTCALL (*read16)(v810_timestamp_t , uint32), uint32 MDFN_FASTCALL (*read32)(v810_timestamp_t , uint32))
{
 IORead8 = read8;
 IORead16 = read16;
 IORead32 = read32;
}

void V810_SetIOWriteHandlers(void MDFN_FASTCALL (*write8)(v810_timestamp_t , uint32, uint8), void MDFN_FASTCALL (*write16)(v810_timestamp_t , uint32, uint16), void MDFN_FASTCALL (*write32)(v810_timestamp_t , uint32, uint32))
{
 IOWrite8 = write8;
 IOWrite16 = write16;
 IOWrite32 = write32;
}


INLINE void V810_SetFlag(uint32 n, bool condition)
{
 S_REG[PSW] &= ~n;

 if(condition)
  S_REG[PSW] |= n;
}
	
INLINE void V810_SetSZ(uint32 value)
{
 V810_SetFlag(PSW_Z, !value);
 V810_SetFlag(PSW_S, value & 0x80000000);
}

#define SetPREG(n, val) { P_REG[n] = val; }

INLINE void V810_SetSREG(v810_timestamp_t timestamp, unsigned int which, uint32 value)
{
	switch(which)
	{
	 default:	// Reserved
#if 0
		printf("LDSR to reserved system register: 0x%02x : 0x%08x\n", which, value);
#endif
		break;

         case ECR:      // Read-only
                break;

         case PIR:      // Read-only (obviously)
                break;

         case TKCW:     // Read-only
                break;

	 case EIPSW:
	 case FEPSW:
              	S_REG[which] = value & 0xFF3FF;
		break;

	 case PSW:
              	S_REG[which] = value & 0xFF3FF;
		RecalcIPendingCache();
		break;

	 case EIPC:
	 case FEPC:
		S_REG[which] = value & 0xFFFFFFFE;
		break;

	 case ADDTRE:
  	        S_REG[ADDTRE] = value & 0xFFFFFFFE;
#if 0
        	printf("Address trap(unemulated): %08x\n", value);
#endif
		break;

	 case CHCW:
              	S_REG[CHCW] = value & 0x2;

              	switch(value & 0x31)
              	{
              	 default:
#if 0
                   printf("Undefined cache control bit combination: %08x\n", value);
#endif
                   break;

              	 case 0x00: break;

              	 case 0x01: V810_CacheClear(timestamp, (value >> 20) & 0xFFF, (value >> 8) & 0xFFF);
                            break;

              	 case 0x10: V810_CacheDump(timestamp, value & ~0xFF);
                            break;

              	 case 0x20: V810_CacheRestore(timestamp, value & ~0xFF);
                            break;
               	}
		break;
	}
}

INLINE uint32 V810_GetSREG(unsigned int which)
{
#if 0
   if(which != 24 && which != 25 && which >= 8)
      printf("STSR from reserved system register: 0x%02x", which);
#endif
	return S_REG[which];
}

#define V810_RB_SETPC(new_pc_raw) 										\
			  {										\
			   const uint32 new_pc = new_pc_raw;	/* So V810_RB_SETPC(RB_GETPC()) won't mess up */	\
			   {										\
			    PC_ptr = &FastMap[(new_pc) >> V810_FAST_MAP_SHIFT][(new_pc)];		\
			    PC_base = PC_ptr - (new_pc);						\
			   }										\
			  }

#define RB_PCRELCHANGE(delta) { 				\
				{				\
				 uint32 PC_tmp = RB_GETPC();	\
				 PC_tmp += (delta);		\
				 V810_RB_SETPC(PC_tmp);		\
				}					\
			      }

#define RB_INCPCBY2()	{ PC_ptr += 2; }
#define RB_INCPCBY4()   { PC_ptr += 4; }

#define RB_DECPCBY2()   { PC_ptr -= 2; }
#define RB_DECPCBY4()   { PC_ptr -= 4; }

//
// Define fast mode defines
//
#define RB_GETPC()      	((uint32)(PC_ptr - PC_base))

#ifdef _MSC_VER
#define RB_RDOP(PC_offset, b) LoadU16_LE((uint16 *)&PC_ptr[PC_offset])
#else
#define RB_RDOP(PC_offset, ...) LoadU16_LE((uint16 *)&PC_ptr[PC_offset])
#endif

v810_timestamp_t V810_Run(int32 MDFN_FASTCALL (*event_handler)(const v810_timestamp_t timestamp))
{
	Running = true;
	#define RB_ADDBT(n,o,p)
	#define RB_CPUHOOK(n)

	#include "v810_oploop.inc"

	#undef RB_CPUHOOK
	#undef RB_ADDBT
	return(v810_timestamp);
}

// Undefine fast mode defines
//
#undef RB_GETPC
#undef RB_RDOP

void V810_Exit(void)
{
	Running = false;
}

uint32 V810_GetPC(void)
{
 return(PC_ptr - PC_base);
}

void V810_SetPC(uint32 new_pc)
{
  PC_ptr = &FastMap[new_pc >> V810_FAST_MAP_SHIFT][new_pc];
  PC_base = PC_ptr - new_pc;
}

uint32 V810_GetPR(const unsigned int which)
{
 return(which ? P_REG[which] : 0);
}

void V810_SetPR(const unsigned int which, uint32 value)
{
 if(which)
  P_REG[which] = value;
}

uint32 V810_GetSR(const unsigned int which)
{
 return(V810_GetSREG(which));
}


#define BSTR_OP_MOV dst_cache &= ~(1 << dstoff); dst_cache |= ((src_cache >> srcoff) & 1) << dstoff;
#define BSTR_OP_NOT dst_cache &= ~(1 << dstoff); dst_cache |= (((src_cache >> srcoff) & 1) ^ 1) << dstoff;

#define BSTR_OP_XOR dst_cache ^= ((src_cache >> srcoff) & 1) << dstoff;
#define BSTR_OP_OR dst_cache |= ((src_cache >> srcoff) & 1) << dstoff;
#define BSTR_OP_AND dst_cache &= ~((((src_cache >> srcoff) & 1) ^ 1) << dstoff);

#define BSTR_OP_XORN dst_cache ^= (((src_cache >> srcoff) & 1) ^ 1) << dstoff;
#define BSTR_OP_ORN dst_cache |= (((src_cache >> srcoff) & 1) ^ 1) << dstoff;
#define BSTR_OP_ANDN dst_cache &= ~(((src_cache >> srcoff) & 1) << dstoff);

INLINE uint32 V810_BSTR_RWORD(v810_timestamp_t timestamp, uint32 A)
{
 if(MemReadBus32[A >> 24])
 {
  timestamp += 2;
  return(MemRead32(timestamp, A));
 }
 else
 {
  uint32 ret;

  timestamp += 2;
  ret = MemRead16(timestamp, A);
 
  timestamp += 2;
  ret |= MemRead16(timestamp, A | 2) << 16;
  return(ret);
 }
}

INLINE void V810_BSTR_WWORD(v810_timestamp_t timestamp, uint32 A, uint32 V)
{
 if(MemWriteBus32[A >> 24])
 {
  timestamp += 2;
  MemWrite32(timestamp, A, V);
 }
 else
 {
  timestamp += 2;
  MemWrite16(timestamp, A, V & 0xFFFF);

  timestamp += 2;
  MemWrite16(timestamp, A | 2, V >> 16);
 }
}

#define DO_BSTR(op) { 						\
                while(len)					\
                {						\
                 if(!have_src_cache)                            \
                 {                                              \
		  have_src_cache = true;			\
                  src_cache = V810_BSTR_RWORD(timestamp, src);       \
                 }                                              \
								\
		 if(!have_dst_cache)				\
		 {						\
		  have_dst_cache = true;			\
                  dst_cache = V810_BSTR_RWORD(timestamp, dst);       \
                 }                                              \
								\
		 op;						\
                 srcoff = (srcoff + 1) & 0x1F;			\
                 dstoff = (dstoff + 1) & 0x1F;			\
		 len--;						\
								\
		 if(!srcoff)					\
		 {                                              \
		  src += 4;					\
		  have_src_cache = false;			\
		 }                                              \
								\
                 if(!dstoff)                                    \
                 {                                              \
                  V810_BSTR_WWORD(timestamp, dst, dst_cache);        \
                  dst += 4;                                     \
		  have_dst_cache = false;			\
		  if(timestamp >= next_event_ts)		\
		   break;					\
                 }                                              \
                }						\
                if(have_dst_cache)				\
                 V810_BSTR_WWORD(timestamp, dst, dst_cache);		\
		}

INLINE bool V810_Do_BSTR_Search(v810_timestamp_t timestamp, const int inc_mul, unsigned int bit_test)
{
        uint32 srcoff = (P_REG[27] & 0x1F);
        uint32 len = P_REG[28];
        uint32 bits_skipped = P_REG[29];
        uint32 src = (P_REG[30] & 0xFFFFFFFC);
	bool found = false;

	#if 0
	// TODO: Better timing.
	if(!in_bstr)	// If we're just starting the execution of this instruction(kind of spaghetti-code), so FIXME if we change
			// bstr handling in v810_oploop.inc
	{
	 timestamp += 13 - 1;
	}
	#endif

	while(len)
	{
		if(!have_src_cache)
		{
		 have_src_cache = true;
		 timestamp++;
		 src_cache = V810_BSTR_RWORD(timestamp, src);
		}

		if(((src_cache >> srcoff) & 1) == bit_test)
		{
		 found = true;

		 /* Fix the bit offset and word address to "1 bit before" it was found */
		 srcoff -= inc_mul * 1;
		 if(srcoff & 0x20)		/* Handles 0x1F->0x20(0x00) and 0x00->0xFFFF... */
		 {
		  src -= inc_mul * 4;
		  srcoff &= 0x1F;
		 }
		 break;
		}
	        srcoff = (srcoff + inc_mul * 1) & 0x1F;
		bits_skipped++;
	        len--;

	        if(!srcoff)
		{
	         have_src_cache = false;
		 src += inc_mul * 4;
		 if(timestamp >= next_event_ts)
		  break;
		}
	}

        P_REG[27] = srcoff;
        P_REG[28] = len;
        P_REG[29] = bits_skipped;
        P_REG[30] = src;


        if(found)               // Set Z flag to 0 if the bit was found
         V810_SetFlag(PSW_Z, 0);
        else if(!len)           // ...and if the search is over, and the bit was not found, set it to 1
         V810_SetFlag(PSW_Z, 1);

        if(found)               // Bit found, so don't continue the search.
         return(false);

        return((bool)len);      // Continue the search if any bits are left to search.
}

bool bstr_subop(v810_timestamp_t timestamp, int sub_op, int arg1)
{
   if((sub_op >= 0x10) || (!(sub_op & 0x8) && sub_op >= 0x4))
   {
#if 0
      printf("%08x\tBSR Error: %04x\n", PC,sub_op);
#endif

      V810_SetPC(V810_GetPC() - 2);
      V810_Exception(INVALID_OP_HANDLER_ADDR, ECODE_INVALID_OP);

      return(false);
   }

   // printf("BSTR: %02x, %02x %02x; src: %08x, dst: %08x, len: %08x\n", sub_op, P_REG[27], P_REG[26], P_REG[30], P_REG[29], P_REG[28]);

   if(sub_op & 0x08)
   {
      uint32 dstoff = (P_REG[26] & 0x1F);
      uint32 srcoff = (P_REG[27] & 0x1F);
      uint32 len =     P_REG[28];
      uint32 dst =    (P_REG[29] & 0xFFFFFFFC);
      uint32 src =    (P_REG[30] & 0xFFFFFFFC);

            switch(sub_op)
            {
            case ORBSU: DO_BSTR(BSTR_OP_OR); break;

            case ANDBSU: DO_BSTR(BSTR_OP_AND); break;

            case XORBSU: DO_BSTR(BSTR_OP_XOR); break;

            case MOVBSU: DO_BSTR(BSTR_OP_MOV); break;

            case ORNBSU: DO_BSTR(BSTR_OP_ORN); break;

            case ANDNBSU: DO_BSTR(BSTR_OP_ANDN); break;

            case XORNBSU: DO_BSTR(BSTR_OP_XORN); break;

            case NOTBSU: DO_BSTR(BSTR_OP_NOT); break;
            }

      P_REG[26] = dstoff; 
      P_REG[27] = srcoff;
      P_REG[28] = len;
      P_REG[29] = dst;
      P_REG[30] = src;

      return((bool)P_REG[28]);
   }
#if 0
   else
      printf("BSTR Search: %02x\n", sub_op);
#endif

   return(V810_Do_BSTR_Search(timestamp, ((sub_op & 1) ? -1 : 1), (sub_op & 0x2) >> 1));
}

INLINE void V810_SetFPUOPNonFPUFlags(uint32 result)
{
                 // Now, handle flag setting
                 V810_SetFlag(PSW_OV, 0);

                 if(!(result & 0x7FFFFFFF)) // Check to see if exponent and mantissa are 0
		 {
		  // If Z flag is set, S and CY should be clear, even if it's negative 0(confirmed on real thing with subf.s, at least).
                  V810_SetFlag(PSW_Z, 1);
                  V810_SetFlag(PSW_S, 0);
                  V810_SetFlag(PSW_CY, 0);
		 }
                 else
		 {
                  V810_SetFlag(PSW_Z, 0);
                  V810_SetFlag(PSW_S, result & 0x80000000);
                  V810_SetFlag(PSW_CY, result & 0x80000000);
		 }
                 //printf("MEOW: %08x\n", S_REG[PSW] & (PSW_S | PSW_CY));
}

INLINE bool V810_CheckFPInputException(uint32 fpval)
{
 // Zero isn't a subnormal! (OR IS IT *DUN DUN DUNNN* ;b)
 if(!(fpval & 0x7FFFFFFF))
  return(false);

 switch((fpval >> 23) & 0xFF)
 {
  case 0x00: // Subnormal		
  case 0xFF: // NaN or infinity
	{
	 //puts("New FPU FRO");

	 S_REG[PSW] |= PSW_FRO;

	 V810_SetPC(V810_GetPC() - 4);
	 V810_Exception(FPU_HANDLER_ADDR, ECODE_FRO);
	}
	return(true);	// Yes, exception occurred
 }
 return(false);	// No, no exception occurred.
}

bool V810_FPU_DoesExceptionKillResult(void)
{
 if(float_exception_flags & float_flag_invalid)
  return(true);

 if(float_exception_flags & float_flag_divbyzero)
  return(true);


 // Return false here, so that the result of this calculation IS put in the output register.
 // (Incidentally, to get the result of operations on overflow to match a real V810, required a bit of hacking of the SoftFloat code to "wrap" the exponent
 // on overflow,
 // rather than generating an infinity.  The wrapping behavior is specified in IEE 754 AFAIK, and is useful in cases where you divide a huge number
 // by another huge number, and fix the result afterwards based on the number of overflows that occurred.  Probably requires some custom assembly code,
 // though.  And it's the kind of thing you'd see in an engineering or physics program, not in a perverted video game :b).
 // Oh, and just a note to self, FPR is NOT set when an overflow occurs.  Or it is in certain cases?
 if(float_exception_flags & float_flag_overflow)
  return(false);

 return(false);
}

void V810_FPU_DoException(void)
{
 if(float_exception_flags & float_flag_invalid)
 {
  //puts("New FPU Invalid");

  S_REG[PSW] |= PSW_FIV;

  V810_SetPC(V810_GetPC() - 4);
  V810_Exception(FPU_HANDLER_ADDR, ECODE_FIV);

  return;
 }

 if(float_exception_flags & float_flag_divbyzero)
 {
  //puts("New FPU Divide by Zero");

  S_REG[PSW] |= PSW_FZD;

  V810_SetPC(V810_GetPC() - 4);
  V810_Exception(FPU_HANDLER_ADDR, ECODE_FZD);

  return;
 }

 if(float_exception_flags & float_flag_underflow)
 {
  //puts("New FPU Underflow");

  S_REG[PSW] |= PSW_FUD;
 }

 if(float_exception_flags & float_flag_inexact)
 {
  S_REG[PSW] |= PSW_FPR;
  //puts("New FPU Precision Degradation");
 }

 // FPR can be set along with overflow, so put the overflow exception handling at the end here(for Exception() messes with PSW).
 if(float_exception_flags & float_flag_overflow)
 {
  //puts("New FPU Overflow");

  S_REG[PSW] |= PSW_FOV;

  V810_SetPC(V810_GetPC() - 4);
  V810_Exception(FPU_HANDLER_ADDR, ECODE_FOV);
 }
}

bool V810_IsSubnormal(uint32 fpval)
{
 if( ((fpval >> 23) & 0xFF) == 0 && (fpval & ((1 << 23) - 1)) )
  return(true);

 return(false);
}

INLINE void V810_FPU_Math_Template(float32 (*func)(float32, float32), uint32 arg1, uint32 arg2)
{
 if(V810_CheckFPInputException(P_REG[arg1]) || V810_CheckFPInputException(P_REG[arg2]))
 {

 }
 else
 {
  uint32 result;

  float_exception_flags = 0;
  result = func(P_REG[arg1], P_REG[arg2]);

  if(V810_IsSubnormal(result))
  {
   float_exception_flags |= float_flag_underflow;
   float_exception_flags |= float_flag_inexact;
  }

  //printf("Result: %08x, %02x; %02x\n", result, (result >> 23) & 0xFF, float_exception_flags);

  if(!V810_FPU_DoesExceptionKillResult())
  {
   // Force it to +/- zero before setting S/Z based off of it(confirmed with subf.s on real V810, at least).
   if(float_exception_flags & float_flag_underflow)
    result &= 0x80000000;

   V810_SetFPUOPNonFPUFlags(result);
   SetPREG(arg1, result);
  }
  V810_FPU_DoException();
 }
}

void fpu_subop(v810_timestamp_t timestamp, int sub_op, int arg1, int arg2)
{
  switch(sub_op)
  {
   case XB: timestamp++;	// Unknown
	    P_REG[arg1] = (P_REG[arg1] & 0xFFFF0000) | ((P_REG[arg1] & 0xFF) << 8) | ((P_REG[arg1] & 0xFF00) >> 8);
	    return;

   case XH: timestamp++;	// Unknown
	    P_REG[arg1] = (P_REG[arg1] << 16) | (P_REG[arg1] >> 16);
	    return;

   // Does REV use arg1 or arg2 for the source register?
   case REV: timestamp++;	// Unknown
#if 0
		printf("Revvie bits\n");
#endif
	     {
	      // Public-domain code snippet from: http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel
      	      uint32 v = P_REG[arg2]; // 32-bit word to reverse bit order

	      // swap odd and even bits
	      v = ((v >> 1) & 0x55555555) | ((v & 0x55555555) << 1);
	      // swap consecutive pairs
	      v = ((v >> 2) & 0x33333333) | ((v & 0x33333333) << 2);
	      // swap nibbles ... 
	      v = ((v >> 4) & 0x0F0F0F0F) | ((v & 0x0F0F0F0F) << 4);
	      // swap bytes
	      v = ((v >> 8) & 0x00FF00FF) | ((v & 0x00FF00FF) << 8);
	      // swap 2-byte long pairs
	      v = ( v >> 16             ) | ( v               << 16);

	      P_REG[arg1] = v;
	     }
	     return;

   case MPYHW: timestamp += 9 - 1;	// Unknown?
	       P_REG[arg1] = (int32)(int16)(P_REG[arg1] & 0xFFFF) * (int32)(int16)(P_REG[arg2] & 0xFFFF);
	       return;
  }

 switch(sub_op) 
 {
        // Virtual-Boy specific(probably!)
	default:
		{
		 V810_SetPC(V810_GetPC() - 4);
                 V810_Exception(INVALID_OP_HANDLER_ADDR, ECODE_INVALID_OP);
		}
		break;

	case CVT_WS: 
		timestamp += 5;
		{
		 uint32 result;

                 float_exception_flags = 0;
		 result = int32_to_float32((int32)P_REG[arg2]);

		 if(!V810_FPU_DoesExceptionKillResult())
		 {
		  SetPREG(arg1, result);
		  V810_SetFPUOPNonFPUFlags(result);
		 }
#if 0
		 else
		  puts("Exception on CVT.WS?????");	/* This shouldn't happen, but just in case there's a bug... */
#endif
		 V810_FPU_DoException();
		}
		break;	// End CVT.WS

	case CVT_SW:
		timestamp += 8;
                if(V810_CheckFPInputException(P_REG[arg2]))
                {

                }
		else
		{
		 int32 result;

                 float_exception_flags = 0;
		 result = float32_to_int32(P_REG[arg2]);

		 if(!V810_FPU_DoesExceptionKillResult())
		 {
		  SetPREG(arg1, result);
                  V810_SetFlag(PSW_OV, 0);
                  V810_SetSZ(result);
		 }
		 V810_FPU_DoException();
		}
		break;	// End CVT.SW

	case ADDF_S: timestamp += 8;
		     V810_FPU_Math_Template(float32_add, arg1, arg2);
		     break;

	case SUBF_S: timestamp += 11;
		     V810_FPU_Math_Template(float32_sub, arg1, arg2);
		     break;

        case CMPF_S: timestamp += 6;
		     // Don't handle this like subf.s because the flags
		     // have slightly different semantics(mostly regarding underflow/subnormal results) (confirmed on real V810).
                     if(V810_CheckFPInputException(P_REG[arg1]) || V810_CheckFPInputException(P_REG[arg2]))
                     {

                     }
		     else
		     {
		      V810_SetFlag(PSW_OV, 0);

		      if(float32_eq(P_REG[arg1], P_REG[arg2]))
		      {
		       V810_SetFlag(PSW_Z, 1);
		       V810_SetFlag(PSW_S, 0);
		       V810_SetFlag(PSW_CY, 0);
		      }
		      else
		      {
		       V810_SetFlag(PSW_Z, 0);

		       if(float32_lt(P_REG[arg1], P_REG[arg2]))
		       {
		        V810_SetFlag(PSW_S, 1);
		        V810_SetFlag(PSW_CY, 1);
		       }
		       else
		       {
		        V810_SetFlag(PSW_S, 0);
		        V810_SetFlag(PSW_CY, 0);
                       }
		      }
		     }	// else of if(CheckFP...
                     break;

	case MULF_S: timestamp += 7;
		     V810_FPU_Math_Template(float32_mul, arg1, arg2);
		     break;

	case DIVF_S: timestamp += 43;
		     V810_FPU_Math_Template(float32_div, arg1, arg2);
		     break;

	case TRNC_SW:
                timestamp += 7;

		if(V810_CheckFPInputException(P_REG[arg2]))
		{

		}
		else
                {
                 int32 result;

		 float_exception_flags = 0;
                 result = float32_to_int32_round_to_zero(P_REG[arg2]);

                 if(!V810_FPU_DoesExceptionKillResult())
                 {
                  SetPREG(arg1, result);
		  V810_SetFlag(PSW_OV, 0);
		  V810_SetSZ(result);
                 }
		 V810_FPU_DoException();
                }
                break;	// end TRNC.SW
	}
}

// Generate exception
void V810_Exception(uint32 handler, uint16 eCode) 
{
 // Exception overhead is unknown.

#if 0
    printf("Exception: %08x %04x\n", handler, eCode);
#endif

    // Invalidate our bitstring state(forces the instruction to be re-read, and the r/w buffers reloaded).
    in_bstr = false;
    have_src_cache = false;
    have_dst_cache = false;

    if(S_REG[PSW] & PSW_NP) // Fatal exception
    {
#if 0
     printf("Fatal exception; Code: %08x, ECR: %08x, PSW: %08x, PC: %08x\n", eCode, S_REG[ECR], S_REG[PSW], PC);
#endif
     Halted = HALT_FATAL_EXCEPTION;
     IPendingCache = 0;
     return;
    }
    else if(S_REG[PSW] & PSW_EP)  //Double Exception
    {
     S_REG[FEPC] = V810_GetPC();
     S_REG[FEPSW] = S_REG[PSW];

     S_REG[ECR] = (S_REG[ECR] & 0xFFFF) | (eCode << 16);
     S_REG[PSW] |= PSW_NP;
     S_REG[PSW] |= PSW_ID;
     S_REG[PSW] &= ~PSW_AE;

     V810_SetPC(0xFFFFFFD0);
     IPendingCache = 0;
     return;
    }
    else 	// Regular exception
    {
     S_REG[EIPC] = V810_GetPC();
     S_REG[EIPSW] = S_REG[PSW];
     S_REG[ECR] = (S_REG[ECR] & 0xFFFF0000) | eCode;
     S_REG[PSW] |= PSW_EP;
     S_REG[PSW] |= PSW_ID;
     S_REG[PSW] &= ~PSW_AE;

     V810_SetPC(handler);
     IPendingCache = 0;
     return;
    }
}

int V810_StateAction(StateMem *sm, int load, int data_only)
{
 uint32 *cache_tag_temp = NULL;
 uint32 *cache_data_temp = NULL;
 bool *cache_data_valid_temp = NULL;
 uint32 PC_tmp = V810_GetPC();

 int32 next_event_ts_delta = next_event_ts - v810_timestamp;

 SFORMAT StateRegs[] =
 {
  SFARRAY32(P_REG, 32),
  SFARRAY32(S_REG, 32),
  SFVARN(PC_tmp, "PC"),
  SFVARN(Halted, "Halted"),

  SFVARN(lastop, "lastop"),

  SFARRAY32(cache_tag_temp, 128),
  SFARRAY32(cache_data_temp, 128 * 2),
  SFARRAYB(cache_data_valid_temp, 128 * 2),

  SFVARN(ilevel, "ilevel"),		// Perhaps remove in future?
  SFVARN(next_event_ts_delta, "next_event_ts_delta"),

  // Bitstring stuff:
  SFVARN(src_cache, "src_cache"),
  SFVARN(dst_cache, "dst_cache"),
  SFVARN_BOOL(have_src_cache, "have_src_cache"),
  SFVARN_BOOL(have_dst_cache, "have_dst_cache"),
  SFVARN_BOOL(in_bstr, "in_bstr"),
  SFVARN(in_bstr_to, "in_bstr_to"),

  SFEND
 };

 int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "V810", false);

 if(load)
 {
  // std::max is sanity check for a corrupted save state to not crash emulation,
  // std::min<int64>(0x7FF... is a sanity check and for the case where next_event_ts is set to an extremely large value to
  // denote that it's not happening anytime soon, which could cause an overflow if our current timestamp is larger
  // than what it was when the state was saved.
  next_event_ts = max(v810_timestamp, min(0x7FFFFFFF, (int64)v810_timestamp + next_event_ts_delta));

  RecalcIPendingCache();

  V810_SetPC(PC_tmp);
 }

 return(ret);
}
