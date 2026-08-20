makedir:=$(makedir)/glfw

sources+=$(call List,glfw/Sourcefile)

opts+=-DDIRECTFULLSCREEN -D__GLFW__ -DHAVE_MIXER
libs+= -lGL -lGLU -lglfw -lSDL2 -lSDL2_mixer

ifdef FREEBSD
# on FreeBSD, we have to link to libpthread explicitly
libs+=-lpthread
endif

ifdef MINGW
libs+=-mconsole
endif

NOOPENMPT=1
NOGME=1
NOHW=1
NOUPNP=1
SDL=0
