# SRB2 Core

if(${CMAKE_SYSTEM} MATCHES "Android")
	add_library(SRB2 SHARED)
else()
	add_executable(SRB2 MACOSX_BUNDLE WIN32)
endif()

# Core sources
target_sourcefile(c)
target_sources(SRB2 PRIVATE comptime.c md5.c config.h.in)

### Configuration
set(SRB2_CONFIG_EXTERNAL_ASSETS OFF CACHE BOOL
	"Bundle and hash external assets during configuration. Recommended when packaging.")
set(SRB2_CONFIG_HAVE_PNG ON CACHE BOOL
	"Enable PNG support. Depends on zlib, so will be disabled if you don't enable that too.")
set(SRB2_CONFIG_HAVE_ZLIB ON CACHE BOOL
	"Enable zlib support.")
set(SRB2_CONFIG_HAVE_GME ON CACHE BOOL
	"Enable GME support.")
set(SRB2_CONFIG_HAVE_OPENMPT ON CACHE BOOL
	"Enable OpenMPT support.")
set(SRB2_CONFIG_HAVE_THREADS ON CACHE BOOL
	"Enable multithreading support.")
set(SRB2_CONFIG_HWRENDER ON CACHE BOOL
	"Enable hardware rendering (OpenGL and SDL GPU).")
set(SRB2_CONFIG_OPENGL ON CACHE BOOL
	"Enable the OpenGL renderer. Needs hardware rendering.")
set(SRB2_CONFIG_SDLGPU ON CACHE BOOL
	"Enable the SDL GPU renderer (Vulkan, Direct3D 12, Metal). Needs hardware rendering.")
set(SRB2_CONFIG_HAVE_SHADERC ON CACHE BOOL
	"Use shaderc if found, so that the SDL GPU renderer can use the GLSL shaders of addons. The game's own shaders don't need it.")
set(SRB2_CONFIG_HAVE_SPIRV_CROSS ON CACHE BOOL
	"Use SPIRV-Cross if found, so that the SDL GPU renderer can use the shaders of addons with Metal. The game's own shaders don't need it.")
set(SRB2_CONFIG_SDLGPU_PRECOMPILED_SHADERS OFF CACHE BOOL
	"Use the SDL GPU renderer's precompiled Vulkan and Metal shaders. Otherwise they are compiled during the build, with shaderc and SPIRV-Cross, and the precompiled ones are only used without them or when cross compiling. Direct3D 12's are always precompiled.")
set(SRB2_CONFIG_STATIC_OPENGL OFF CACHE BOOL
	"Use statically linked OpenGL. NOT RECOMMENDED.")
set(SRB2_CONFIG_DEDICATED OFF CACHE BOOL
	"Build a dedicated server.")
set(SRB2_CONFIG_DEBUGMODE OFF CACHE BOOL
	"Build a version with additional debug information.")
set(SRB2_CONFIG_EXECINFO ON CACHE BOOL
	"Enable stack trace dump support.")
set(SRB2_CONFIG_LTO ON CACHE BOOL
	"Enable link time optimizations, improves performance at the cost of longer link times.")
set(SRB2_CONFIG_NATIVESCREENRES OFF CACHE BOOL
	"Additional resolution options menu from the Android port.")

if(${SRB2_CONFIG_DEDICATED})
	set(SRB2_CONFIG_HAVE_GME NO)
	set(SRB2_CONFIG_HAVE_OPENMPT NO)
	set(SRB2_CONFIG_HWRENDER NO)
endif()

if(${CMAKE_SYSTEM} MATCHES "Emscripten")
	set(SRB2_CONFIG_HWRENDER OFF)
	set(SRB2_CONFIG_EXECINFO OFF)
endif()

### use internal libraries?
if(${CMAKE_SYSTEM} MATCHES "Windows" OR ${CMAKE_SYSTEM} MATCHES "Android" OR ${CMAKE_SYSTEM} MATCHES "iOS") ### set on Windows, Android, and iOS only
	set(SRB2_CONFIG_USE_INTERNAL_LIBRARIES OFF CACHE BOOL
		"Use SRB2's internal copies of required dependencies (SDL3, PNG, zlib, GME, OpenMPT).")
endif()

# Android and iOS have no desktop OpenGL: they only get the SDL GPU renderer (Vulkan on Android, Metal on iOS).
# shaderc and SPIRV-Cross are left out, since pkg-config would find the ones of the computer building the game.
if(${CMAKE_SYSTEM} MATCHES "Android")
	set(SRB2_CONFIG_USE_INTERNAL_LIBRARIES ON)
	set(SRB2_CONFIG_HWRENDER ON)
	set(SRB2_CONFIG_SDLGPU ON)
	set(SRB2_CONFIG_OPENGL OFF)
	set(SRB2_CONFIG_HAVE_SHADERC OFF)
	set(SRB2_CONFIG_HAVE_SPIRV_CROSS OFF)
	set(SRB2_CONFIG_LTO OFF)
	set(SRB2_CONFIG_NATIVESCREENRES ON)
endif()

if(${CMAKE_SYSTEM} MATCHES "iOS")
	set(SRB2_CONFIG_USE_INTERNAL_LIBRARIES ON)
	set(SRB2_CONFIG_HWRENDER ON)
	set(SRB2_CONFIG_SDLGPU ON)
	set(SRB2_CONFIG_OPENGL OFF)
	set(SRB2_CONFIG_HAVE_SHADERC OFF)
	set(SRB2_CONFIG_HAVE_SPIRV_CROSS OFF)
	set(SRB2_CONFIG_NATIVESCREENRES ON)
endif()

add_subdirectory(blua)

if(${SRB2_CONFIG_HAVE_GME})
	if(${SRB2_CONFIG_USE_INTERNAL_LIBRARIES})
		set(GME_FOUND ON)
		if(${CMAKE_SYSTEM} MATCHES "Windows")
			set(GME_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/gme/include)
			if(${SRB2_SYSTEM_BITS} EQUAL 64)
				set(GME_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/gme/win64 -lgme")
			else() # 32-bit
				set(GME_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/gme/win32 -lgme")
			endif()
		elseif(${CMAKE_SYSTEM} MATCHES "Android")
			set(GME_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/libgme/include)
			set(GME_LIBRARIES "${CMAKE_SOURCE_DIR}/thirdparty/libgme/libs/${CMAKE_ANDROID_ARCH_ABI}/libgme.so")
		elseif(${CMAKE_SYSTEM} MATCHES "iOS")
			set(GME_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/libgme/iOS/include)
			set(GME_LIBRARIES "${CMAKE_SOURCE_DIR}/thirdparty/libgme/iOS/gme.xcframework")
		endif()
	elseif(${CMAKE_SYSTEM} MATCHES "Emscripten")
		set(GME_FOUND ON)
		set(GME_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/gme/include)
		set(GME_LIBRARIES "${CMAKE_SOURCE_DIR}/libs/gme/emscripten/libgme.a")
	else()
		find_package(GME)
	endif()
	if(${GME_FOUND})
		set(SRB2_HAVE_GME ON)
		target_compile_definitions(SRB2 PRIVATE -DHAVE_GME)
	else()
		message(WARNING "You have specified that GME is available but it was not found.")
	endif()
endif()

if(${SRB2_CONFIG_HAVE_OPENMPT})
	if(${SRB2_CONFIG_USE_INTERNAL_LIBRARIES})
		set(OPENMPT_FOUND ON)
		if(${CMAKE_SYSTEM} MATCHES "Windows")
			set(OPENMPT_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/libopenmpt/inc)
			if(${SRB2_SYSTEM_BITS} EQUAL 64)
				set(OPENMPT_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/libopenmpt/lib/x86_64/mingw -lopenmpt")
			else() # 32-bit
				set(OPENMPT_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/libopenmpt/lib/x86/mingw -lopenmpt")
			endif()
		elseif(${CMAKE_SYSTEM} MATCHES "Android")
			set(OPENMPT_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/libopenmpt/include)
			set(OPENMPT_LIBRARIES "${CMAKE_SOURCE_DIR}/thirdparty/libopenmpt/libs/${CMAKE_ANDROID_ARCH_ABI}/libopenmpt.so")
		elseif(${CMAKE_SYSTEM} MATCHES "iOS")
			set(OPENMPT_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/libopenmpt/iOS/include)
			set(OPENMPT_LIBRARIES "${CMAKE_SOURCE_DIR}/thirdparty/libopenmpt/iOS/libopenmpt.xcframework")
		endif()
	elseif(${CMAKE_SYSTEM} MATCHES "Emscripten")
		set(OPENMPT_FOUND ON)
		set(OPENMPT_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/libopenmpt/inc)
		set(OPENMPT_LIBRARIES "${CMAKE_SOURCE_DIR}/libs/libopenmpt/emscripten/libopenmpt.a")
	else()
		find_package(OPENMPT)
	endif()
	if(${OPENMPT_FOUND})
		set(SRB2_HAVE_OPENMPT ON)
		target_compile_definitions(SRB2 PRIVATE -DHAVE_OPENMPT)
	else()
		message(WARNING "You have specified that OpenMPT is available but it was not found.")
	endif()
endif()

if(${SRB2_CONFIG_HAVE_THREADS})
	set(SRB2_HAVE_THREADS ON)
	target_sources(SRB2 PRIVATE i_threads.c)
	target_compile_definitions(SRB2 PRIVATE -DHAVE_THREADS)
endif()

if(${SRB2_CONFIG_HAVE_ZLIB})
	if(${SRB2_CONFIG_USE_INTERNAL_LIBRARIES})
		set(ZLIB_FOUND ON)
		if(${CMAKE_SYSTEM} MATCHES "Windows")
			set(ZLIB_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/zlib)
			if(${SRB2_SYSTEM_BITS} EQUAL 64)
				set(ZLIB_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/zlib/win32 -lz64")
			else() # 32-bit
				set(ZLIB_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/zlib/win32 -lz32")
			endif()
		elseif(${CMAKE_SYSTEM} MATCHES "Android")
			set(ZLIB_LIBRARIES "-lz")
		elseif(${CMAKE_SYSTEM} MATCHES "iOS")
			# Apple ships zlib as a system library on iOS; no custom build needed.
			set(ZLIB_LIBRARIES "-lz")
		endif()
	elseif(${CMAKE_SYSTEM} MATCHES Emscripten)
		set(ZLIB_FOUND ON)
	else()
		find_package(ZLIB)
	endif()
	if(${ZLIB_FOUND})
		set(SRB2_HAVE_ZLIB ON)
		target_compile_definitions(SRB2 PRIVATE -DHAVE_ZLIB)
	else()
		message(WARNING "You have specified that ZLIB is available but it was not found. SRB2 may not compile correctly.")
	endif()
endif()

if(${SRB2_CONFIG_HAVE_PNG} AND ${SRB2_CONFIG_HAVE_ZLIB})
	if (${ZLIB_FOUND})
		if(${SRB2_CONFIG_USE_INTERNAL_LIBRARIES})
			set(PNG_FOUND ON)
			if(${CMAKE_SYSTEM} MATCHES "Windows")
				set(PNG_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/libpng-src)
				if(${SRB2_SYSTEM_BITS} EQUAL 64)
					set(PNG_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/libpng-src/projects -lpng64")
				else() # 32-bit
					set(PNG_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/libpng-src/projects -lpng32")
				endif()
			elseif(${CMAKE_SYSTEM} MATCHES "Android")
				set(PNG_LIBRARIES libpng::libpng)
			elseif(${CMAKE_SYSTEM} MATCHES "iOS")
				set(PNG_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/libpng/iOS/include)
				set(PNG_LIBRARIES "${CMAKE_SOURCE_DIR}/thirdparty/libpng/iOS/libpng.xcframework")
			endif()
		elseif(${CMAKE_SYSTEM} MATCHES Emscripten)
			set(PNG_FOUND ON)
		else()
			find_package(PNG)
		endif()
		if(${PNG_FOUND})
			set(SRB2_HAVE_PNG ON)
			target_compile_definitions(SRB2 PRIVATE -DHAVE_PNG)
			target_compile_definitions(SRB2 PRIVATE -D_LARGEFILE64_SOURCE)
			target_sources(SRB2 PRIVATE apng.c)
		else()
			message(WARNING "You have specified that PNG is available but it was not found. SRB2 may not compile correctly.")
		endif()
	endif()
endif()

if(${SRB2_CONFIG_HAVE_CURL})
	if(${SRB2_CONFIG_USE_INTERNAL_LIBRARIES})
		set(CURL_FOUND ON)
		if(${CMAKE_SYSTEM} MATCHES "Windows")
			set(CURL_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/curl/include)
			if(${SRB2_SYSTEM_BITS} EQUAL 64)
				set(CURL_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/curl/lib64 -lcurl")
			else() # 32-bit
				set(CURL_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/curl/lib32 -lcurl")
			endif()
		endif()
	else()
		find_package(CURL)
	endif()
	if(${CURL_FOUND})
		set(SRB2_HAVE_CURL ON)
		target_compile_definitions(SRB2 PRIVATE -DHAVE_CURL)
	else()
		message(WARNING "You have specified that CURL is available but it was not found. SRB2 may not compile correctly.")
	endif()
endif()

if(${SRB2_CONFIG_HWRENDER} AND NOT ${SRB2_CONFIG_OPENGL} AND NOT ${SRB2_CONFIG_SDLGPU})
	message(WARNING "Hardware rendering is enabled, but neither OpenGL nor SDL GPU is. Not setting HWRENDER.")
	set(SRB2_CONFIG_HWRENDER OFF)
endif()

if(${SRB2_CONFIG_HWRENDER})
	target_compile_definitions(SRB2 PRIVATE -DHWRENDER)
	add_subdirectory(hardware)
	if(${SRB2_CONFIG_OPENGL})
		target_compile_definitions(SRB2 PRIVATE -DHWRENDER_OPENGL)
		target_sources(SRB2 PRIVATE hardware/r_opengl/r_opengl.c)
	endif()
	if(${SRB2_CONFIG_SDLGPU})
		target_compile_definitions(SRB2 PRIVATE -DHWRENDER_SDLGPU)
		target_sources(SRB2 PRIVATE
			hardware/r_sdlgpu/r_sdlgpu.c
			hardware/r_sdlgpu/gpu_glsl.c)
		find_package(PkgConfig)

		# SPIRV-Cross's C API: the shared library, or else the static one (like Homebrew's),
		# whose .pc leaves out the other SPIRV-Cross libraries and the C++ runtime it needs
		macro(srb2_find_spirv_cross)
			if(PKG_CONFIG_FOUND)
				pkg_check_modules(SPIRV_CROSS spirv-cross-c-shared)
				if(NOT SPIRV_CROSS_FOUND)
					pkg_check_modules(SPIRV_CROSS spirv-cross-c)
					if(SPIRV_CROSS_FOUND)
						foreach(lib msl hlsl cpp reflect glsl core)
							find_library(SRB2_SPIRV_CROSS_${lib} spirv-cross-${lib} HINTS ${SPIRV_CROSS_LIBRARY_DIRS} NO_DEFAULT_PATH)
							if(SRB2_SPIRV_CROSS_${lib})
								list(APPEND SPIRV_CROSS_LINK_LIBRARIES ${SRB2_SPIRV_CROSS_${lib}})
							endif()
						endforeach()
						if(APPLE)
							list(APPEND SPIRV_CROSS_LINK_LIBRARIES c++)
						else()
							list(APPEND SPIRV_CROSS_LINK_LIBRARIES stdc++)
						endif()
					endif()
				endif()
			endif()
		endmacro()

		# Only the shader formats of the platform's graphics APIs are built in
		if(APPLE)
			set(SRB2_SDLGPU_FORMATS msl) # Metal
		elseif(WIN32)
			set(SRB2_SDLGPU_FORMATS spirv dxil) # Vulkan, Direct3D 12
		else()
			set(SRB2_SDLGPU_FORMATS spirv) # Vulkan
		endif()

		# The game's Vulkan and Metal shaders: compiled now, or else the precompiled ones
		set(SRB2_SDLGPU_SHADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/hardware/r_sdlgpu/precompiled")
		if(NOT ${SRB2_CONFIG_SDLGPU_PRECOMPILED_SHADERS})
			if(CMAKE_CROSSCOMPILING)
				message(STATUS "The SDL GPU shaders can't be compiled when cross compiling: using the precompiled ones.")
			else()
				if(PKG_CONFIG_FOUND)
					pkg_check_modules(SHADERC shaderc)
				endif()
				srb2_find_spirv_cross()
				if(SHADERC_FOUND AND SPIRV_CROSS_FOUND)
					add_executable(srb2-sdlgpu-shaders
						"${CMAKE_SOURCE_DIR}/tools/sdlgpu-shaders/gen.c"
						hardware/r_sdlgpu/gpu_glsl.c)
					target_compile_definitions(srb2-sdlgpu-shaders PRIVATE -DHAVE_SHADERC -DHAVE_SPIRV_CROSS)
					# not in bin/, next to the game
					set_target_properties(srb2-sdlgpu-shaders PROPERTIES
						RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sdlgpu-shaders")
					target_include_directories(srb2-sdlgpu-shaders PRIVATE ${SHADERC_INCLUDE_DIRS} ${SPIRV_CROSS_INCLUDE_DIRS})
					# full paths, so that the tool finds them when it runs, wherever they are
					target_link_libraries(srb2-sdlgpu-shaders PRIVATE ${SHADERC_LINK_LIBRARIES} ${SPIRV_CROSS_LINK_LIBRARIES})

					# only the formats the game is built with, but DXIL, which needs DXC
					set(SRB2_SDLGPU_SHADERS_DIR "${CMAKE_CURRENT_BINARY_DIR}/sdlgpu-shaders")
					set(SRB2_SDLGPU_GENERATED)
					set(SRB2_SDLGPU_GEN_ARGS)
					foreach(format IN LISTS SRB2_SDLGPU_FORMATS)
						if(NOT format STREQUAL "dxil")
							list(APPEND SRB2_SDLGPU_GENERATED "${SRB2_SDLGPU_SHADERS_DIR}/${format}.c")
							list(APPEND SRB2_SDLGPU_GEN_ARGS --${format} "${SRB2_SDLGPU_SHADERS_DIR}/${format}.c")
						endif()
					endforeach()
					add_custom_command(
						OUTPUT ${SRB2_SDLGPU_GENERATED}
						COMMAND ${CMAKE_COMMAND} -E make_directory "${SRB2_SDLGPU_SHADERS_DIR}"
						COMMAND srb2-sdlgpu-shaders ${SRB2_SDLGPU_GEN_ARGS}
						DEPENDS srb2-sdlgpu-shaders
						COMMENT "Compiling the SDL GPU shaders"
						VERBATIM)
					# for their #include "../gpu_glsl.h"
					set_source_files_properties(${SRB2_SDLGPU_GENERATED} PROPERTIES
						GENERATED TRUE
						INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/hardware/r_sdlgpu/precompiled")
				else()
					message(STATUS "shaderc and SPIRV-Cross were not both found: using the precompiled SDL GPU shaders.")
				endif()
			endif()
		endif()
		foreach(format IN LISTS SRB2_SDLGPU_FORMATS)
			string(TOUPPER ${format} FORMAT)
			target_compile_definitions(SRB2 PRIVATE -DSDLGPU_${FORMAT})
			if(format STREQUAL "dxil") # always precompiled
				target_sources(SRB2 PRIVATE hardware/r_sdlgpu/precompiled/dxil.c)
			else()
				target_sources(SRB2 PRIVATE "${SRB2_SDLGPU_SHADERS_DIR}/${format}.c")
			endif()
		endforeach()
		if(${SRB2_CONFIG_HAVE_SHADERC})
			if(PKG_CONFIG_FOUND)
				pkg_check_modules(SHADERC shaderc)
			endif()
			if(SHADERC_FOUND)
				target_compile_definitions(SRB2 PRIVATE -DHAVE_SHADERC)
				target_include_directories(SRB2 PRIVATE ${SHADERC_INCLUDE_DIRS})
				target_link_directories(SRB2 PRIVATE ${SHADERC_LIBRARY_DIRS})
				target_link_libraries(SRB2 PRIVATE ${SHADERC_LIBRARIES})
			else()
				message(STATUS "shaderc was not found: the SDL GPU renderer won't be able to use the GLSL shaders of addons.")
			endif()
		endif()
		if(${SRB2_CONFIG_HAVE_SPIRV_CROSS})
			srb2_find_spirv_cross()
			if(SPIRV_CROSS_FOUND)
				target_compile_definitions(SRB2 PRIVATE -DHAVE_SPIRV_CROSS)
				target_include_directories(SRB2 PRIVATE ${SPIRV_CROSS_INCLUDE_DIRS})
				target_link_libraries(SRB2 PRIVATE ${SPIRV_CROSS_LINK_LIBRARIES})
			else()
				message(STATUS "SPIRV-Cross was not found: the SDL GPU renderer won't be able to use the shaders of addons with Metal.")
			endif()
		endif()
	endif()
endif()

if(${SRB2_CONFIG_HWRENDER} AND ${SRB2_CONFIG_OPENGL} AND ${SRB2_CONFIG_STATIC_OPENGL})
	find_package(OpenGL)
	if(${OPENGL_FOUND})
		target_compile_definitions(SRB2 PRIVATE -DHWRENDER)
		target_compile_definitions(SRB2 PRIVATE -DSTATIC_OPENGL)
	else()
		message(WARNING "You have specified static opengl but opengl was not found. Not setting HWRENDER.")
	endif()
endif()

if(${SRB2_CONFIG_EXECINFO})
else()
	target_compile_definitions(SRB2 PRIVATE -DNOEXECINFO)
	message(STATUS "You have disabled stack trace dump support")
endif()

if(SRB2_CONFIG_LTO AND NOT SRB2_CONFIG_DEBUGMODE)
	set_property(TARGET SRB2 PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(${SRB2_CONFIG_NATIVESCREENRES})
	target_compile_definitions(SRB2 PRIVATE -DNATIVESCREENRES)
endif()

if(${CMAKE_SYSTEM} MATCHES "iOS")
	# since C++ libraries are statically linked
	set_property(TARGET SRB2 PROPERTY LINKER_LANGUAGE CXX)
endif()

# Targets

# Compatibility flag with later versions of GCC
# We should really fix our code to not need this
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mno-ms-bitfields")
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT ${CMAKE_SYSTEM} MATCHES "Emscripten")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-absolute-value")
endif()

target_compile_definitions(SRB2 PRIVATE -DCMAKECONFIG)

if(${SRB2_CONFIG_DEBUGMODE})
	target_compile_definitions(SRB2 PRIVATE -D_DEBUG -DPARANOIA -DRANGECHECK)
else()
	target_compile_definitions(SRB2 PRIVATE -DNDEBUG)
endif()

if(${CMAKE_SYSTEM} MATCHES "Android")
	# Support 16 KB page sizes on Android
	target_link_options(SRB2 PRIVATE
		"-Wl,-z,max-page-size=16384"
		"-Wl,-z,common-page-size=16384"
	)
	target_compile_definitions(SRB2 PRIVATE -DNOMUMBLE -DSTDC_HEADERS)
endif()

#add_library(SRB2Core STATIC
#	${SRB2_CORE_SOURCES}
#	${SRB2_CORE_HEADERS}
#	${SRB2_CORE_RENDER_SOURCES}
#	${SRB2_CORE_GAME_SOURCES}
#	${SRB2_LUA_SOURCES}
#	${SRB2_LUA_HEADERS}
#	${SRB2_BLUA_SOURCES}
#	${SRB2_BLUA_HEADERS}
#)

if(${SRB2_CONFIG_DEDICATED})
	add_subdirectory(dedicated)
else()
	add_subdirectory(sdl)
endif()

if(${CMAKE_SYSTEM} MATCHES Darwin AND NOT ${SRB2_CONFIG_DEDICATED})
	if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
		set(SRB2_MACOS_ICON_DEPLOYMENT_TARGET "11.0")
	else()
		set(SRB2_MACOS_ICON_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}")
	endif()
	# srb2.icon needs actool from Xcode 26+, older versions skip it without failing;
	# otherwise, bundle the classic Srb2mac.icns instead
	set_target_properties(SRB2 PROPERTIES MACOSX_BUNDLE_ICON_FILE Srb2mac.icns)
	execute_process(COMMAND xcrun --find actool
		RESULT_VARIABLE SRB2_ACTOOL_RESULT
		OUTPUT_QUIET ERROR_QUIET
	)
	execute_process(COMMAND xcodebuild -version
		OUTPUT_VARIABLE SRB2_XCODE_VERSION
		ERROR_QUIET
	)
	string(REGEX REPLACE "^Xcode ([0-9.]+).*" "\\1" SRB2_XCODE_VERSION "${SRB2_XCODE_VERSION}")
	if(SRB2_ACTOOL_RESULT EQUAL 0 AND SRB2_XCODE_VERSION VERSION_GREATER_EQUAL 26)
		add_custom_command(TARGET SRB2 POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:SRB2>/Resources"
			COMMAND xcrun actool --compile "$<TARGET_BUNDLE_CONTENT_DIR:SRB2>/Resources"
				--platform macosx
				--minimum-deployment-target "${SRB2_MACOS_ICON_DEPLOYMENT_TARGET}"
				--app-icon srb2
				--output-partial-info-plist /dev/null
				> /dev/null
				"${CMAKE_CURRENT_SOURCE_DIR}/sdl/srb2.icon"
			VERBATIM
		)
	else()
		message(STATUS "actool from Xcode 26+ not found, using sdl/macosx/Srb2mac.icns")
		set_source_files_properties(sdl/macosx/Srb2mac.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
		target_sources(SRB2 PRIVATE sdl/macosx/Srb2mac.icns)
	endif()
endif()

if(${CMAKE_SYSTEM} MATCHES "Emscripten")
	add_subdirectory(web)
endif()

if(${CMAKE_SYSTEM} MATCHES "Windows")
	add_subdirectory(win32)
endif()

if(${CMAKE_SYSTEM} MATCHES "Android")
	add_subdirectory(android-jni)
endif()

if(NOT ${SRB2_SDL_AVAILABLE} AND NOT ${SRB2_WIN32_AVAILABLE})
	message(FATAL_ERROR "There are no targets available to build an SRB2 executable. :(")
endif()

# strip debug symbols into separate file when using gcc.
# to be consistent with Makefile, don't generate for OS X.
if((CMAKE_COMPILER_IS_GNUCC) AND NOT ("${CMAKE_SYSTEM_NAME}" MATCHES Darwin))
	if((${CMAKE_BUILD_TYPE} MATCHES Debug) OR (${CMAKE_BUILD_TYPE} MATCHES RelWithDebInfo))
		if(${CMAKE_BUILD_TYPE} MATCHES Debug)
			set(OBJCOPY_ONLY_KEEP_DEBUG "--only-keep-debug")
		endif()
		message(STATUS "Will make separate debug symbols in *.debug")
		add_custom_command(TARGET SRB2 POST_BUILD
			COMMAND ${OBJCOPY} ${OBJCOPY_ONLY_KEEP_DEBUG} $<TARGET_FILE:SRB2> $<TARGET_FILE:SRB2>.debug
			COMMAND ${OBJCOPY} --strip-debug $<TARGET_FILE:SRB2>
			COMMAND ${OBJCOPY} --add-gnu-debuglink=$<TARGET_FILE:SRB2>.debug $<TARGET_FILE:SRB2>
		)
	endif()
endif()
