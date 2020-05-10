project(melonDS-sdl)

set(CMAKE_CXX_STANDARD 14)
add_compile_options(-Wall -Wextra -Werror -pedantic)

add_executable(melonDS-sdl
        main.cpp
        ConfPath.cpp
        EmuWindow.cpp
        Platform.cpp
        PlatformConfig.cpp
        InputDialog.cpp
        Emulator.cpp
        )

find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL2 REQUIRED sdl2 SDL2_gfx)

target_include_directories(melonDS-sdl PUBLIC ${SDL2_INCLUDE_DIR})
target_link_libraries(melonDS-sdl core ${SDL2_LIBRARIES})

install(TARGETS melonDS-sdl RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
