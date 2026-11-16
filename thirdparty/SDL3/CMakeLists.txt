add_library(sdl3 INTERFACE)
target_include_directories(sdl3 INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(sdl3 INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/libs/${CMAKE_ANDROID_ARCH_ABI}/libSDL3.so")
add_library(SDL3::SDL3 ALIAS sdl3)
