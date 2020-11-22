PRGNAME     = vb.elf
CC          = gcc
CXX 		= g++

#### Configuration

# Possible values : retrostone, rs97, rs90
PORT = gcw0
# Possible values : alsa, oss, portaudio
SOUND_ENGINE = sdl

#### End of Configuration

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"

INCLUDES	= -Ilibretro-common/include -Isrc
INCLUDES	+= -Ishell/headers -Ishell/video/$(PORT) -Ishell/audio -Ishell/scalers -Ishell/input/sdl -Ishell/fonts -Ishell/menu
INCLUDES	+= -Imednafen -I./mednafen/vb -I./mednafen/sound -I. -Ishell/emu -Imednafen/include -Ishell/input -Imednafen/video

DEFINES		= -DLSB_FIRST -DINLINE="inline" -DINLINE="inline" -DNDEBUG -DWANT_STEREO_SOUND
DEFINES		+= -DMEDNAFEN_VERSION_NUMERIC=931
DEFINES		+= -DWANT_16BPP -DFRONTEND_SUPPORTS_RGB565 -DFRAMESKIP

CFLAGS		= -O2 -pg -fno-common -Wall -Wextra -Wunused-value $(INCLUDES) $(DEFINES)
CXXFLAGS	= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
LDFLAGS     = -lc -lgcc -lstdc++ -lm -lSDL -lz -pthread -lportaudio

ifeq ($(SOUND_ENGINE), alsa)
LDFLAGS 		+= -lasound
endif
ifeq ($(SOUND_ENGINE), portaudio)
LDFLAGS 		+= -lasound -lportaudio
endif

# Files to be compiled
SRCDIR 		=  ./src ./shell ./shell/scalers ./shell/emu ./shell/menu
SRCDIR		+= ./shell/input/sdl/ ./shell/video/$(PORT) ./shell/audio/$(SOUND_ENGINE)
SRCDIR		+= ./mednafen ./mednafen/vb ./mednafen/sound ./mednafen/hw_cpu/v810 ./mednafen/hw_cpu/v810/fpu-new

VPATH		= $(SRCDIR)
SRC_C		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.c))
SRC_CPP		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.cpp))
OBJ_C		= $(notdir $(patsubst %.c, %.o, $(SRC_C)))
OBJ_CPP		= $(notdir $(patsubst %.cpp, %.o, $(SRC_CPP)))
OBJS		= $(OBJ_C) $(OBJ_CPP)

# Rules to make executable
$(PRGNAME): $(OBJS)  
	$(CC) $(CFLAGS) -std=gnu99 -o $(PRGNAME) $^ $(LDFLAGS)
	
$(OBJ_CPP) : %.o : %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
	
$(OBJ_C) : %.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<
	
clean:
	rm -f $(PRGNAME)$(EXESUFFIX) *.o
