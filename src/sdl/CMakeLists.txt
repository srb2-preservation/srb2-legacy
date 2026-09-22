# Declare SDL2 interface sources

set(SRB2_CONFIG_SDL2_USEMIXER ON CACHE BOOL "Use SDL2_mixer or regular sdl sound")

if(${SRB2_CONFIG_SDL2_USEMIXER})
	if(${SRB2_CONFIG_USE_INTERNAL_LIBRARIES})
		set(SDL2_MIXER_FOUND ON)
		if(${CMAKE_SYSTEM} MATCHES "Windows")
			if(${SRB2_SYSTEM_BITS} EQUAL 64)
				set(SDL2_MIXER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/SDL2_mixer/x86_64-w64-mingw32/include/SDL2)
				set(SDL2_MIXER_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/SDL2_mixer/x86_64-w64-mingw32/lib -lSDL2_mixer")
			else() # 32-bit
				set(SDL2_MIXER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/SDL2_mixer/i686-w64-mingw32/include/SDL2)
				set(SDL2_MIXER_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/SDL2_mixer/i686-w64-mingw32/lib -lSDL2_mixer")
			endif()
		elseif(${CMAKE_SYSTEM} MATCHES "Android")
			set(SDL2_MIXER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/SDL2_mixer/include/)
			set(SDL2_MIXER_LIBRARIES "${CMAKE_SOURCE_DIR}/thirdparty/SDL2_mixer/libs/${CMAKE_ANDROID_ARCH_ABI}/libSDL2_mixer.so")
		elseif(${CMAKE_SYSTEM} MATCHES "iOS")
			set(SDL2_MIXER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/SDL2_mixer/include)
			set(SDL2_MIXER_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/SDL2_mixer/iOS")
			set(SDL2_MIXER_LIBRARIES
				"${SDL2_MIXER_ROOT}/SDL2_mixer.xcframework"
				"${SDL2_MIXER_ROOT}/opusfile.xcframework"
				"${SDL2_MIXER_ROOT}/opus.xcframework"
				"${SDL2_MIXER_ROOT}/vorbisfile.xcframework"
				"${SDL2_MIXER_ROOT}/vorbis.xcframework"
				"${SDL2_MIXER_ROOT}/ogg.xcframework"
				"${SDL2_MIXER_ROOT}/wavpack.xcframework"
			)
		endif()
	elseif(${CMAKE_SYSTEM} MATCHES Emscripten)
		set(SDL2_MIXER_FOUND  ON)
		set(SDL2_MIXER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/SDL2_mixer/include)
		set(SDL2_MIXER_LIBRARIES "${CMAKE_SOURCE_DIR}/libs/SDL2_mixer/emscripten/libSDL2_mixer.a")
	else()
		find_package(SDL2_mixer)
	endif()
	if(${SDL2_MIXER_FOUND})
		set(SRB2_HAVE_MIXER ON)
		target_sources(SRB2SDL2 PRIVATE mixer_sound.c)
	else()
		message(WARNING "You specified that SDL2_mixer is available, but it was not found. Falling back to sdl sound.")
		target_sources(SRB2SDL2 PRIVATE sdl_sound.c)
	endif()
else()
	target_sources(SRB2SDL2 PRIVATE sdl_sound.c)
endif()

target_sourcefile(c)

if(${CMAKE_SYSTEM} MATCHES "Android")
	target_sources(SRB2SDL2 PRIVATE SDL_main/SDL_android_main.c)
endif()

target_sources(SRB2SDL2 PRIVATE ogl_sdl.c)

# Dependency
if(${SRB2_CONFIG_USE_INTERNAL_LIBRARIES})
	set(SDL2_FOUND ON)
	if(${CMAKE_SYSTEM} MATCHES "Windows")
		if(${SRB2_SYSTEM_BITS} EQUAL 64)
			set(SDL2_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/SDL2/x86_64-w64-mingw32/include/SDL2)
			set(SDL2_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/SDL2/x86_64-w64-mingw32/lib -lSDL2")
		else() # 32-bit
			set(SDL2_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/SDL2/i686-w64-mingw32/include/SDL2)
			set(SDL2_LIBRARIES "-L${CMAKE_SOURCE_DIR}/libs/SDL2/i686-w64-mingw32/lib -lSDL2")
		endif()
	elseif(${CMAKE_SYSTEM} MATCHES "Android")
		set(SDL2_INCLUDE_DIRS
			${CMAKE_SOURCE_DIR}/thirdparty/SDL2/android/config
			${CMAKE_SOURCE_DIR}/thirdparty/SDL2/include
		)
		set(SDL2_LIBRARIES "${CMAKE_SOURCE_DIR}/thirdparty/SDL2/libs/${CMAKE_ANDROID_ARCH_ABI}/libSDL2.so")
	elseif(${CMAKE_SYSTEM} MATCHES "iOS")
		set(SDL2_INCLUDE_DIRS
			${CMAKE_SOURCE_DIR}/thirdparty/SDL2/iOS/config
			${CMAKE_SOURCE_DIR}/thirdparty/SDL2/include
		)
		set(SDL2_LIBRARIES
			"${CMAKE_SOURCE_DIR}/thirdparty/SDL2/iOS/SDL2.xcframework"
			"${CMAKE_SOURCE_DIR}/thirdparty/SDL2/iOS/SDL2main.xcframework"
		)
	endif()
elseif(${CMAKE_SYSTEM} MATCHES Emscripten)
	set(SDL2_FOUND ON)
else()
	find_package(SDL2 CONFIG REQUIRED)
	find_package(SDL2 REQUIRED)
endif()

if(${SDL2_FOUND})
	if(${CMAKE_SYSTEM} MATCHES Windows)
		target_sources(SRB2SDL2 PRIVATE
			../win32/win_dbg.c
			../win32/Srb2win.rc)
	endif()

	if(${CMAKE_SYSTEM} MATCHES Darwin)
		target_sources(SRB2SDL2 PRIVATE
			macosx/mac_resources.c
			macosx/mac_resources.h
		)
	endif()

	if(${CMAKE_SYSTEM} MATCHES "iOS")
		target_sources(SRB2SDL2 PRIVATE
			ios/ios_resources.c
			ios/ios_resources.h
		)
	endif()

	if(${CMAKE_SYSTEM} MATCHES "Windows")
		set_target_properties(SRB2SDL2 PROPERTIES OUTPUT_NAME srb2legacywin)
	elseif(${CMAKE_SYSTEM} MATCHES "Linux")
		set_target_properties(SRB2SDL2 PROPERTIES OUTPUT_NAME lsdl2srb2legacy)
	elseif(${CMAKE_SYSTEM} MATCHES "Android")
		set_target_properties(SRB2SDL2 PROPERTIES OUTPUT_NAME main)
	else()
		set_target_properties(SRB2SDL2 PROPERTIES OUTPUT_NAME srb2legacy)
	endif()

	if(${CMAKE_SYSTEM} MATCHES Emscripten)
		set_target_properties(SRB2SDL2 PROPERTIES OUTPUT_NAME srb2legacy SUFFIX ".js")
	endif()

	if(${CMAKE_SYSTEM} MATCHES Darwin)
		find_library(CORE_FOUNDATION_LIBRARY "CoreFoundation")
		target_link_libraries(SRB2SDL2 PRIVATE
			${CORE_FOUNDATION_LIBRARY}
		)
		set_target_properties(SRB2SDL2 PROPERTIES OUTPUT_NAME "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
		set_target_properties(SRB2SDL2 PROPERTIES
			MACOSX_BUNDLE_BUNDLE_NAME "SRB2 Legacy"
			MACOSX_BUNDLE_BUNDLE_VERSION ${SRB2_VERSION}
			MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macosx/Info.plist.in"
		)
	endif()

	if(${CMAKE_SYSTEM} MATCHES "iOS")
		set_target_properties(SRB2SDL2 PROPERTIES
			OUTPUT_NAME "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}"
			MACOSX_BUNDLE_BUNDLE_NAME "SRB2 Legacy"
			MACOSX_BUNDLE_BUNDLE_VERSION ${SRB2_VERSION}
			MACOSX_BUNDLE_GUI_IDENTIFIER "org.stjr.srb2legacy"
			XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.stjr.srb2legacy"
			MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
			# must be set here since Xcode overwrites the setting in Info.plist.in
			XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
		)
		target_compile_definitions(SRB2SDL2 PRIVATE -DSDLMAIN)
		set(SRB2_IOS_FRAMEWORKS
			UIKit Foundation CoreFoundation CoreGraphics QuartzCore
			CoreAudio AudioToolbox AVFoundation CoreMotion
			GameController CoreHaptics CoreBluetooth Metal OpenGLES
		)
		foreach(fwname ${SRB2_IOS_FRAMEWORKS})
			find_library(SRB2_IOS_FRAMEWORK_${fwname} ${fwname})
			target_link_libraries(SRB2SDL2 PRIVATE ${SRB2_IOS_FRAMEWORK_${fwname}})
		endforeach()
		target_link_libraries(SRB2SDL2 PRIVATE c++)
	endif()

	target_link_libraries(SRB2SDL2 PRIVATE
		${SDL2_LIBRARIES}
		${SDL2_MIXER_LIBRARIES}
		${GME_LIBRARIES}
		${OPENMPT_LIBRARIES}
		${MIXERX_LIBRARIES}
		${PNG_LIBRARIES}
		${ZLIB_LIBRARIES}
		${OPENGL_LIBRARIES}
		${CURL_LIBRARIES}
	)

	if(${CMAKE_SYSTEM} MATCHES Linux)
		target_link_libraries(SRB2SDL2 PRIVATE
			m
			rt
		)
	endif()

	#target_link_libraries(SRB2SDL2 PRIVATE SRB2Core)

	if(${CMAKE_SYSTEM} MATCHES Windows)
		target_link_libraries(SRB2SDL2 PRIVATE
			ws2_32
		)
		target_compile_options(SRB2SDL2 PRIVATE
			-U_WINDOWS
		)
	endif()

	if("${CMAKE_SYSTEM_NAME}" MATCHES "OpenBSD")
		target_link_libraries(SRB2SDL2 PRIVATE pthread execinfo m)
	endif()

	if(${CMAKE_SYSTEM} MATCHES "Haiku")
		target_compile_definitions(SRB2SDL2 PRIVATE -DNOEXECINFO)
		target_link_libraries(SRB2SDL2 PRIVATE network)
	endif()

	target_include_directories(SRB2SDL2 PRIVATE
		${SDL2_INCLUDE_DIRS}
		${SDL2_MIXER_INCLUDE_DIRS}
		${GME_INCLUDE_DIRS}
		${OPENMPT_INCLUDE_DIRS}
		${PNG_INCLUDE_DIRS}
		${ZLIB_INCLUDE_DIRS}
		${OPENGL_INCLUDE_DIRS}
	)

	if(${SRB2_HAVE_MIXER})
		target_compile_definitions(SRB2SDL2 PRIVATE -DHAVE_MIXER -DSOUND=SOUND_MIXER)
	endif()

	target_compile_definitions(SRB2SDL2 PRIVATE
		-DDIRECTFULLSCREEN
		-DHAVE_SDL
	)

	#### Installation ####
	if(${CMAKE_SYSTEM} MATCHES Darwin OR ${CMAKE_SYSTEM} MATCHES "iOS")
		install(TARGETS SRB2SDL2
			BUNDLE DESTINATION .
		)
		set_property(TARGET SRB2SDL2 PROPERTY INSTALL_RPATH_USE_LINK_PATH ON)
	else()
		install(TARGETS SRB2SDL2 SRB2SDL2
			RUNTIME DESTINATION .
		)
		if ((${CMAKE_BUILD_TYPE} MATCHES Debug) OR (${CMAKE_BUILD_TYPE} MATCHES RelWithDebInfo))
			set(SRB2_DEBUG_INSTALL OFF CACHE BOOL "Insert *.debug file into the install directory or package.")
			if (${SRB2_DEBUG_INSTALL})
				install(FILES $<TARGET_FILE:SRB2SDL2>.debug
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
						find_library(SRB2_SDL2_DLL_${dllname} "${defaultname}"
							HINTS ${CMAKE_SOURCE_DIR}/libs/dll-binaries/x86_64
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL2/x86_64-w64-mingw32/bin
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL2_mixer/x86_64-w64-mingw32/bin
							HINTS ${CMAKE_SOURCE_DIR}/libs/libopenmpt/bin/x86_64/mingw
						)
					else()
						find_library(SRB2_SDL2_DLL_${dllname} "${defaultname}"
							HINTS ${CMAKE_SOURCE_DIR}/libs/dll-binaries/i686
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL2/i686-w64-mingw32/bin
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL2_mixer/i686-w64-mingw32/bin
							HINTS ${CMAKE_SOURCE_DIR}/libs/libopenmpt/bin/x86/mingw
						)
					endif()
				else()
					if(${SRB2_SYSTEM_BITS} EQUAL 64)
						find_library(SRB2_SDL2_DLL_${dllname} "${defaultname}"
							HINTS ${CMAKE_SOURCE_DIR}/libs/dll-binaries/x86_64
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL2/lib/x64
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL2_mixer/lib/x64
							HINTS ${CMAKE_SOURCE_DIR}/libs/libopenmpt/bin/x86_64/mingw
						)
					else()
						find_library(SRB2_SDL2_DLL_${dllname} "${defaultname}"
							HINTS ${CMAKE_SOURCE_DIR}/libs/dll-binaries/i686
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL2/lib/x86
							HINTS ${CMAKE_SOURCE_DIR}/libs/SDL2_mixer/lib/x86
							HINTS ${CMAKE_SOURCE_DIR}/libs/libopenmpt/bin/x86/mingw
						)
					endif()
				endif()

				list(APPEND win_extra_dll_list ${SRB2_SDL2_DLL_${dllname}})
			else()
				find_library(SRB2_SDL2_DLL_${dllname} "${defaultname}")
				list(APPEND win_extra_dll_list ${SRB2_SDL2_DLL_${dllname}})
	endif()
		endmacro()
		getwinlib(SDL2 "SDL2.dll")
		if(${SRB2_CONFIG_SDL2_USEMIXER})
			getwinlib(SDL2_mixer "SDL2_mixer.dll")
			getwinlib(libogg_0 "libogg-0.dll")
			getwinlib(libvorbis_0 "libvorbis-0.dll")
			getwinlib(libvorbisfile_3 "libvorbisfile-3.dll")
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
		copy_files_to_build_dir(SRB2SDL2 win_extra_dll_list)
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

	set(SRB2_SDL2_AVAILABLE YES PARENT_SCOPE)
else()
	message(WARNING "SDL2 was not found, so the SDL2 target will not be available.")
	set(SRB2_SDL2_AVAILABLE NO PARENT_SCOPE)
endif()
