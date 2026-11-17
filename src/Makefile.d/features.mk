#
# Makefile for feature flags.
#

passthru_opts+=\
	NONET NO_IPV6 NOHW NOMD5 NOPOSTPROCESSING\
	MOBJCONSISTANCY PACKETDROP ZDEBUG\
	HAVE_MINIUPNPC NOEXECINFO NATIVESCREENRES\

# build with debugging information
ifdef DEBUGMODE
MOBJCONSISTANCY=1
PACKETDROP=1
opts+=-DPARANOIA -DRANGECHECK
endif

ifndef NOHW
ifdef NOOPENGL
ifdef NOSDLGPU
$(error NOOPENGL=1 and NOSDLGPU=1 leave no hardware renderer: use NOHW=1 instead)
endif
endif
opts+=-DHWRENDER
sources+=$(call List,hardware/Sourcefile)
ifndef NOOPENGL
opts+=-DHWRENDER_OPENGL
sources+=hardware/r_opengl/r_opengl.c
endif
ifndef NOSDLGPU
opts+=-DHWRENDER_SDLGPU
sources+=hardware/r_sdlgpu/r_sdlgpu.c
sources+=hardware/r_sdlgpu/gpu_glsl.c
# only the shader formats of the platform's graphics APIs are built in
ifdef MACOS
sdlgpu_formats:=msl
opts+=-DSDLGPU_MSL
else ifdef MINGW
sdlgpu_formats:=spirv dxil
opts+=-DSDLGPU_SPIRV -DSDLGPU_DXIL
else
sdlgpu_formats:=spirv
opts+=-DSDLGPU_SPIRV
endif
sources+=$(foreach format,$(sdlgpu_formats),hardware/r_sdlgpu/precompiled/$(format).c)
ifdef HAVE_SHADERC
opts+=-DHAVE_SHADERC
libs+=-lshaderc_shared
endif
ifdef HAVE_SPIRV_CROSS
opts+=-DHAVE_SPIRV_CROSS $(shell $(PKG_CONFIG) --cflags spirv-cross-c-shared)
libs+=$(shell $(PKG_CONFIG) --libs spirv-cross-c-shared)
endif
endif
endif

ifndef NOMD5
sources+=md5.c
endif

ifndef NOZLIB
ifndef NOPNG
ifdef PNG_PKGCONFIG
$(eval $(call Use_pkg_config,PNG_PKGCONFIG))
else
PNG_CONFIG?=$(call Prefix,libpng-config)
$(eval $(call Configure,PNG,$(PNG_CONFIG) \
	$(if $(PNG_STATIC),--static),,--ldflags))
endif
ifdef LINUX
opts+=-D_LARGEFILE64_SOURCE
endif
opts+=-DHAVE_PNG
sources+=apng.c
endif
endif

ifndef NONET
ifndef NOCURL
CURLCONFIG?=curl-config
$(eval $(call Configure,CURL,$(CURLCONFIG)))
opts+=-DHAVE_CURL
endif
endif

ifdef HAVE_MINIUPNPC
libs+=-lminiupnpc
endif

# (Valgrind is a memory debugger.)
ifdef VALGRIND
VALGRIND_PKGCONFIG?=valgrind
VALGRIND_LDFLAGS=
$(eval $(call Use_pkg_config,VALGRIND))
ZDEBUG=1
opts+=-DHAVE_VALGRIND
endif

default_packages:=\
	GME/libgme/LIBGME\
	OPENMPT/libopenmpt/LIBOPENMPT\
	ZLIB/zlib\

$(foreach p,$(default_packages),\
	$(eval $(call Check_pkg_config,$(p))))
