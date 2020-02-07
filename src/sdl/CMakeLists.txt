project(melonDS-sdl)

add_executable(melonDS-sdl
	main.cpp
	ConfPath.cpp
	EmuWindow.cpp
	Platform.cpp
	PlatformConfig.cpp
)

find_package(SDL2 REQUIRED)

target_include_directories(melonDS-sdl PUBLIC ${SDL2_INCLUDE_DIR} ..)
target_link_libraries(melonDS-sdl core ${SDL2_LIBRARIES})

install(TARGETS melonDS-sdl RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
