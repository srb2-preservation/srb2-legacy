#
# Mingw, if you don't know, that's Win32/Win64
#

ifndef MINGW64
EXENAME?=srb2legacywin.exe
else
EXENAME?=srb2legacywin64.exe
endif

sources+=win32/Srb2win.rc
opts+=-DSTDC_HEADERS
libs+=-ladvapi32 -lkernel32 -lmsvcrt -luser32

nasm_format:=win32

SDL=1

ifndef NOHW
opts+=-DUSE_WGL_SWAP
endif

ifdef MINGW64
libs+=-lws2_32
else
ifdef NO_IPV6
libs+=-lwsock32
else
libs+=-lws2_32
endif
endif

ifndef NONET
ifndef MINGW64 # miniupnc is broken with MINGW64
opts+=-I../libs -DSTATIC_MINIUPNPC
libs+=-L../libs/miniupnpc/mingw$(32) -lws2_32 -liphlpapi
endif
endif

ifndef MINGW64
32=32
x86=x86
i686=i686
else
32=64
x86=x86_64
i686=x86_64
endif

mingw:=$(i686)-w64-mingw32

define _set =
$(1)_CFLAGS?=$($(1)_opts)
$(1)_LDFLAGS?=$($(1)_libs)
endef

lib:=../libs/gme
LIBGME_opts:=-I$(lib)/include
LIBGME_libs:=-L$(lib)/win$(32) -lgme
$(eval $(call _set,LIBGME))

lib:=../libs/libopenmpt
LIBOPENMPT_opts:=-I$(lib)/inc
LIBOPENMPT_libs:=-L$(lib)/lib/$(x86)/mingw -lopenmpt
$(eval $(call _set,LIBOPENMPT))

# SDL3 and SDL3_mixer development libraries for MinGW go in
# libs/SDL3 and libs/SDL3_mixer, like the SDL2 ones did.
lib:=../libs/SDL3_mixer/$(mingw)
SDL_MIXER_opts:=-I$(lib)/include
SDL_MIXER_libs:=-L$(lib)/lib -lSDL3_mixer
$(eval $(call _set,SDL_MIXER))

# SDL3 has no SDL2main library; SDL_main.h takes care of the entry point.
lib:=../libs/SDL3/$(mingw)
SDL_opts:=-I$(lib)/include
SDL_libs:=-L$(lib)/lib -lmingw32 -lSDL3 -mwindows
$(eval $(call _set,SDL))

lib:=../libs/zlib
ZLIB_opts:=-I$(lib)
ZLIB_libs:=-L$(lib)/win32 -lz$(32)
$(eval $(call _set,ZLIB))

ifndef PNG_CONFIG
lib:=../libs/libpng-src
PNG_opts:=-I$(lib)
PNG_libs:=-L$(lib)/projects -lpng$(32)
$(eval $(call _set,PNG))
endif

lib:=../libs/curl
CURL_opts:=-I$(lib)/include
CURL_libs:=-L$(lib)/lib$(32) -lcurl
$(eval $(call _set,CURL))
