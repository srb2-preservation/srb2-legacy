#
# Makefile options for SDL3 backend.
#

#
# SDL...., *looks at Alam*, THIS IS A MESS!
#
# ...a little bird flexes its muscles...
#

makedir:=$(makedir)/SDL

sources+=$(call List,sdl/Sourcefile)
opts+=-DDIRECTFULLSCREEN -DHAVE_SDL

ifdef MACOS
sources+=$(call List,sdl/macosx/Sourcefile)
endif

ifndef NOHW
ifndef NOOPENGL
sources+=sdl/ogl_sdl.c
endif
ifndef NOSDLGPU
sources+=sdl/gpu_sdl.c
endif
endif

ifndef NOHS
ifdef OPENAL
	OBJS+=$(OBJDIR)/s_openal.o
	OPTS+=-DSTATIC3DS
	STATICHS=1
else
ifdef FMOD
	OBJS+=$(OBJDIR)/s_fmod.o
	OPTS+=-DSTATIC3DS
	STATICHS=1
else
ifdef MINGW
ifdef DS3D
	OBJS+=$(OBJDIR)/s_ds3d.o
	OPTS+=-DSTATIC3DS
	STATICHS=1
endif
endif
endif
endif
endif

ifdef NOMIXER
sources+=sdl/sdl_sound.c
else
opts+=-DHAVE_MIXER
sources+=sdl/mixer_sound.c

SDL_MIXER_PKGCONFIG?=sdl3-mixer
SDL_MIXER_CFLAGS?=$(shell $(PKG_CONFIG) $(SDL_MIXER_PKGCONFIG) --cflags)
SDL_MIXER_LDFLAGS?=$(shell $(PKG_CONFIG) $(SDL_MIXER_PKGCONFIG) --libs)
$(eval $(call Propogate_flags,SDL_MIXER))
endif

ifndef NOTHREADS
opts+=-DHAVE_THREADS
sources+=i_threads.c
endif

# SDL3 has no sdl-config script, pkg-config is the way to find it.
SDL_PKGCONFIG?=sdl3
SDL_CFLAGS?=$(shell $(PKG_CONFIG) $(SDL_PKGCONFIG) --cflags)
SDL_LDFLAGS?=$(shell $(PKG_CONFIG) $(SDL_PKGCONFIG) \
		$(if $(STATIC),--static) --libs)
$(eval $(call Propogate_flags,SDL))

ifdef MINGW
ifndef NOSDLMAIN
SDLMAIN=1
endif
endif

ifdef SDLMAIN
opts+=-DSDLMAIN
else
ifdef MINGW
opts+=-Umain
libs+=-mconsole
endif
endif
