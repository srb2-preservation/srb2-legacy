# Declare SDL3 interface sources

set(SRB2_CONFIG_USEMIXER ON CACHE BOOL "Use SDL3_mixer or regular sdl sound")

# Where the prebuilt SDL3 and SDL3_mixer development files are expected
# when using internal libraries, per platform:
#   Windows:    libs/SDL3/<arch>-w64-mingw32, libs/SDL3_mixer/<arch>-w64-mingw32
#   Android:    thirdparty/SDL3/libs/<abi>, thirdparty/SDL3_mixer/libs/<abi>
#   iOS:        thirdparty/SDL3/iOS, thirdparty/SDL3_mixer/iOS (xcframeworks)
#   Emscripten: libs/SDL3_mixer/emscripten (SDL3 itself is an Emscripten port)

if(${SRB2_CONFIG_USEMIXER})
	if(${SRB2_CONFIG_USE_INTERNAL_LIBRARIES})
		set(SDL3_MIXER_FOUND ON)
		if(${CMAKE_SYSTEM} MATCHES "Windows")
			if(${SRB2_SYSTEM_BITS} EQUAL 64)
				set(SDL3_MIXER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/SDL3_mixer/x86_64-w64-mingw32/include)
				set(SDL3_MIXER_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/SDL3_mixer/x86_64-w64-mingw32/lib -lSDL3_mixer")
			else() # 32-bit
				set(SDL3_MIXER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/SDL3_mixer/i686-w64-mingw32/include)
				set(SDL3_MIXER_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/SDL3_mixer/i686-w64-mingw32/lib -lSDL3_mixer")
			endif()
		elseif(${CMAKE_SYSTEM} MATCHES "Android")
			set(SDL3_MIXER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/SDL3_mixer/include/)
			set(SDL3_MIXER_LIBRARIES "${CMAKE_SOURCE_DIR}/thirdparty/SDL3_mixer/libs/${CMAKE_ANDROID_ARCH_ABI}/libSDL3_mixer.so")
		elseif(${CMAKE_SYSTEM} MATCHES "iOS")
			set(SDL3_MIXER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/SDL3_mixer/include)
			set(SDL3_MIXER_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/SDL3_mixer/iOS")
			# SDL3_mixer is a static library; its vendored codecs are separate static libraries
			set(SDL3_MIXER_LIBRARIES
				"${SDL3_MIXER_ROOT}/SDL3_mixer.xcframework"
				"${SDL3_MIXER_ROOT}/vorbisfile.xcframework"
				"${SDL3_MIXER_ROOT}/vorbis.xcframework"
				"${SDL3_MIXER_ROOT}/ogg.xcframework"
			)
		endif()
	elseif(${CMAKE_SYSTEM} MATCHES "Emscripten")
		set(SDL3_MIXER_FOUND  ON)
		set(SDL3_MIXER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/SDL3_mixer/include)
		set(SDL3_MIXER_LIBRARIES "${CMAKE_SOURCE_DIR}/libs/SDL3_mixer/emscripten/libSDL3_mixer.a")
	else()
		find_package(SDL3_mixer CONFIG)
		if(SDL3_mixer_FOUND)
			set(SDL3_MIXER_FOUND ON)
			set(SDL3_MIXER_LIBRARIES SDL3_mixer::SDL3_mixer)
		else()
			set(SDL3_MIXER_FOUND OFF)
		endif()
	endif()
	if(${SDL3_MIXER_FOUND})
		set(SRB2_HAVE_MIXER ON)
		target_sources(SRB2 PRIVATE mixer_sound.c)
	else()
		message(WARNING "You specified that SDL3_mixer is available, but it was not found. Falling back to sdl sound.")
		target_sources(SRB2 PRIVATE sdl_sound.c)
	endif()
else()
	target_sources(SRB2 PRIVATE sdl_sound.c)
endif()

target_sourcefile(c)

if(${CMAKE_SYSTEM} MATCHES "Android")
	target_sources(SRB2 PRIVATE SDL_main/SDL_android_main.c)
endif()

target_sources(SRB2 PRIVATE ogl_sdl.c)
if(${SRB2_CONFIG_HWRENDER} AND ${SRB2_CONFIG_SDLGPU})
	target_sources(SRB2 PRIVATE gpu_sdl.c)
endif()

# Dependency
if(${SRB2_CONFIG_USE_INTERNAL_LIBRARIES})
	set(SDL3_FOUND ON)
	if(${CMAKE_SYSTEM} MATCHES "Windows")
		if(${SRB2_SYSTEM_BITS} EQUAL 64)
			set(SDL3_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/SDL3/x86_64-w64-mingw32/include)
			set(SDL3_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/SDL3/x86_64-w64-mingw32/lib -lSDL3")
		else() # 32-bit
			set(SDL3_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/SDL3/i686-w64-mingw32/include)
			set(SDL3_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/SDL3/i686-w64-mingw32/lib -lSDL3")
		endif()
	elseif(${CMAKE_SYSTEM} MATCHES "Android")
		set(SDL3_INCLUDE_DIRS
			${CMAKE_SOURCE_DIR}/thirdparty/SDL3/include
		)
		set(SDL3_LIBRARIES "${CMAKE_SOURCE_DIR}/thirdparty/SDL3/libs/${CMAKE_ANDROID_ARCH_ABI}/libSDL3.so")
	elseif(${CMAKE_SYSTEM} MATCHES "iOS")
		set(SDL3_INCLUDE_DIRS
			${CMAKE_SOURCE_DIR}/thirdparty/SDL3/include
		)
		# SDL3 has no separate SDL_main library
		set(SDL3_LIBRARIES
			"${CMAKE_SOURCE_DIR}/thirdparty/SDL3/iOS/SDL3.xcframework"
		)
	endif()
elseif(${CMAKE_SYSTEM} MATCHES "Emscripten")
	set(SDL3_FOUND ON)
else()
	find_package(SDL3 REQUIRED CONFIG)
	set(SDL3_FOUND ON)
	set(SDL3_LIBRARIES SDL3::SDL3)
endif()

if(${SDL3_FOUND})
	if(${CMAKE_SYSTEM} MATCHES Windows)
		target_sources(SRB2 PRIVATE
			../win32/win_dbg.c
			../win32/Srb2win.rc)
	endif()

	if(${CMAKE_SYSTEM} MATCHES Darwin)
		target_sources(SRB2 PRIVATE
			macosx/mac_resources.c
			macosx/mac_resources.h
		)
	endif()

	if(${CMAKE_SYSTEM} MATCHES "iOS")
		target_sources(SRB2 PRIVATE
			ios/ios_resources.c
			ios/ios_resources.h
		)
	endif()

	if(${CMAKE_SYSTEM} MATCHES "Windows")
		set_target_properties(SRB2 PROPERTIES OUTPUT_NAME srb2legacywin)
	elseif(${CMAKE_SYSTEM} MATCHES "Linux")
		set_target_properties(SRB2 PROPERTIES OUTPUT_NAME lsdl3srb2legacy)
	elseif(${CMAKE_SYSTEM} MATCHES "Android")
		set_target_properties(SRB2 PROPERTIES OUTPUT_NAME main)
	else()
		set_target_properties(SRB2 PROPERTIES OUTPUT_NAME srb2legacy)
	endif()

	if(${CMAKE_SYSTEM} MATCHES "Emscripten")
		set_target_properties(SRB2 PROPERTIES OUTPUT_NAME srb2legacy SUFFIX ".js")
	endif()

	if(${CMAKE_SYSTEM} MATCHES Darwin)
		find_library(CORE_FOUNDATION_LIBRARY "CoreFoundation")
		target_link_libraries(SRB2 PRIVATE
			${CORE_FOUNDATION_LIBRARY}
		)
		set_target_properties(SRB2 PROPERTIES OUTPUT_NAME "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
		set_target_properties(SRB2 PROPERTIES
			MACOSX_BUNDLE_BUNDLE_NAME "SRB2 Legacy"
			MACOSX_BUNDLE_BUNDLE_VERSION ${SRB2_VERSION}
			MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macosx/Info.plist.in"
		)
	endif()

	if(${CMAKE_SYSTEM} MATCHES "iOS")
		set_target_properties(SRB2 PROPERTIES
			OUTPUT_NAME "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}"
			MACOSX_BUNDLE_BUNDLE_NAME "SRB2 Legacy"
			MACOSX_BUNDLE_BUNDLE_VERSION ${SRB2_VERSION}
			MACOSX_BUNDLE_GUI_IDENTIFIER "org.stjr.srb2legacy"
			XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.stjr.srb2legacy"
			MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
			# must be set here since Xcode overwrites the setting in Info.plist.in
			XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
			# -fno-common: large tentative globals otherwise request 32K alignment, above arm64's 16K max
			XCODE_ATTRIBUTE_GCC_NO_COMMON_BLOCKS YES
			# Xcode enables -Wshorten-64-to-32; no other platform build does
			XCODE_ATTRIBUTE_GCC_WARN_64_TO_32_BIT_CONVERSION NO
		)
		target_compile_definitions(SRB2 PRIVATE -DSDLMAIN)
		set(SRB2_IOS_FRAMEWORKS
			UIKit Foundation CoreFoundation CoreGraphics QuartzCore
			CoreAudio AudioToolbox AVFoundation CoreMotion
			GameController CoreHaptics CoreBluetooth Metal OpenGLES
			CoreMedia CoreVideo # SDL3 camera and Metal renderer
		)
		foreach(fwname ${SRB2_IOS_FRAMEWORKS})
			find_library(SRB2_IOS_FRAMEWORK_${fwname} ${fwname})
			target_link_libraries(SRB2 PRIVATE ${SRB2_IOS_FRAMEWORK_${fwname}})
		endforeach()
	endif()

	target_link_libraries(SRB2 PRIVATE
		${SDL3_MIXER_LIBRARIES}
		${SDL3_LIBRARIES}
		${GME_LIBRARIES}
		${OPENMPT_LIBRARIES}
		${MIXERX_LIBRARIES}
		${PNG_LIBRARIES}
		${ZLIB_LIBRARIES}
		${OPENGL_LIBRARIES}
		${CURL_LIBRARIES}
	)

	if(${CMAKE_SYSTEM} MATCHES Linux)
		target_link_libraries(SRB2 PRIVATE
			m
			rt
		)
	endif()

	#target_link_libraries(SRB2 PRIVATE SRB2Core)

	if(${CMAKE_SYSTEM} MATCHES Windows)
		target_link_libraries(SRB2 PRIVATE
			ws2_32
		)
		target_compile_options(SRB2 PRIVATE
			-U_WINDOWS
		)
	endif()

	if("${CMAKE_SYSTEM_NAME}" MATCHES "OpenBSD")
		target_link_libraries(SRB2 PRIVATE pthread execinfo m)
	endif()

	if(${CMAKE_SYSTEM} MATCHES "Haiku")
		target_compile_definitions(SRB2 PRIVATE -DNOEXECINFO)
		target_link_libraries(SRB2 PRIVATE network)
	endif()

	target_include_directories(SRB2 PRIVATE
		${SDL3_INCLUDE_DIRS}
		${SDL3_MIXER_INCLUDE_DIRS}
		${GME_INCLUDE_DIRS}
		${OPENMPT_INCLUDE_DIRS}
		${PNG_INCLUDE_DIRS}
		${ZLIB_INCLUDE_DIRS}
		${OPENGL_INCLUDE_DIRS}
	)

	if(${SRB2_HAVE_MIXER})
		target_compile_definitions(SRB2 PRIVATE -DHAVE_MIXER -DSOUND=SOUND_MIXER)
	endif()

	target_compile_definitions(SRB2 PRIVATE
		-DDIRECTFULLSCREEN
		-DHAVE_SDL
	)

	#### Installation ####
	if(${CMAKE_SYSTEM} MATCHES Darwin OR ${CMAKE_SYSTEM} MATCHES "iOS")
		install(TARGETS SRB2
			BUNDLE DESTINATION .
		)
		set_property(TARGET SRB2 PROPERTY INSTALL_RPATH_USE_LINK_PATH ON)
	else()
		install(TARGETS SRB2 SRB2
			RUNTIME DESTINATION .
		)
		if ((${CMAKE_BUILD_TYPE} MATCHES Debug) OR (${CMAKE_BUILD_TYPE} MATCHES RelWithDebInfo))
			set(SRB2_DEBUG_INSTALL OFF CACHE BOOL "Insert *.debug file into the install directory or package.")
			if (${SRB2_DEBUG_INSTALL})
				install(FILES $<TARGET_FILE:SRB2>.debug
					DESTINATION .
					OPTIONAL
				)
			endif()
		endif()
	endif()

	if(${CMAKE_SYSTEM} MATCHES Windows)
		set(win_extra_dll_list "")
		macro(getwinlib dllname defaultname)
			if(${SRB2_CONFIG_USE_INTERNAL_LIBRARIES})
				if (${CMAKE_GENERATOR} STREQUAL "MinGW Makefiles")
					if(${SRB2_SYSTEM_BITS} EQUAL 64)
						find_library(SRB2_DLL_${dllname} "${defaultname}"
							HINTS ${CMAKE_SOURCE_DIR}/libs/dll-binaries/x86_64
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL3/x86_64-w64-mingw32/bin
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL3_mixer/x86_64-w64-mingw32/bin
							HINTS ${CMAKE_SOURCE_DIR}/libs/libopenmpt/bin/x86_64/mingw
						)
					else()
						find_library(SRB2_DLL_${dllname} "${defaultname}"
							HINTS ${CMAKE_SOURCE_DIR}/libs/dll-binaries/i686
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL3/i686-w64-mingw32/bin
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL3_mixer/i686-w64-mingw32/bin
							HINTS ${CMAKE_SOURCE_DIR}/libs/libopenmpt/bin/x86/mingw
						)
					endif()
				else()
					if(${SRB2_SYSTEM_BITS} EQUAL 64)
						find_library(SRB2_DLL_${dllname} "${defaultname}"
							HINTS ${CMAKE_SOURCE_DIR}/libs/dll-binaries/x86_64
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL3/lib/x64
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL3_mixer/lib/x64
							HINTS ${CMAKE_SOURCE_DIR}/libs/libopenmpt/bin/x86_64/mingw
						)
					else()
						find_library(SRB2_DLL_${dllname} "${defaultname}"
							HINTS ${CMAKE_SOURCE_DIR}/libs/dll-binaries/i686
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL3/lib/x86
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL3_mixer/lib/x86
							HINTS ${CMAKE_SOURCE_DIR}/libs/libopenmpt/bin/x86/mingw
						)
					endif()
				endif()

				list(APPEND win_extra_dll_list ${SRB2_DLL_${dllname}})
			else()
				find_library(SRB2_DLL_${dllname} "${defaultname}")
				list(APPEND win_extra_dll_list ${SRB2_DLL_${dllname}})
	endif()
		endmacro()
		getwinlib(SDL3 "SDL3.dll")
		if(${SRB2_CONFIG_USEMIXER})
			getwinlib(SDL3_mixer "SDL3_mixer.dll")
		endif()
		if(${SRB2_CONFIG_HAVE_GME})
			getwinlib(libgme "libgme.dll")
		endif()
		if(${SRB2_CONFIG_HAVE_OPENMPT})
			getwinlib(libopenmpt "libopenmpt.dll")
		endif()

		install(PROGRAMS
			${win_extra_dll_list}
			DESTINATION .
		)

		# We also want to copy those DLLs to build directories on MSVC.
		# So we'll add a post_build step.
		copy_files_to_build_dir(SRB2 win_extra_dll_list)
	endif()


	# Mac bundle fixup
	# HACK: THIS IS IMPORTANT! See the escaped \${CMAKE_INSTALL_PREFIX}? This
	# makes it so that var is evaluated LATER during cpack, not right now!
	# This fixes the quirk where the bundled libraries don't land in the final package
	# https://cmake.org/pipermail/cmake/2011-March/043532.html
	#
	# HOWEVER: ${CPACK_PACKAGE_DESCRIPTION_SUMMARY} is NOT escaped, because that var
	# is only available to us at this step. Read the link: ${CMAKE_INSTALL_PREFIX} at
	# this current step points to the CMAKE build folder, NOT the folder that CPACK uses.
	# Therefore, it makes sense to escape that var, but not the other.
	if(${CMAKE_SYSTEM} MATCHES Darwin)
		install(CODE "
			include(BundleUtilities)
			fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/${CPACK_PACKAGE_DESCRIPTION_SUMMARY}.app\"
				\"\"
				/Library/Frameworks
			)"
		)
	endif()

	set(SRB2_SDL_AVAILABLE YES PARENT_SCOPE)
else()
	message(WARNING "SDL3 was not found, so the SDL target will not be available.")
	set(SRB2_SDL_AVAILABLE NO PARENT_SCOPE)
endif()
