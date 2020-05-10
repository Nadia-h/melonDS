/*
    Copyright 2016-2019 Arisotura

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

#include "PlatformConfig.h"

namespace Config {
int default_scale;
int fullscreen;
int use_framelimit;
int use_vsync;

SDL_Keycode keymap[] = {
	SDLK_x, SDLK_z, SDLK_RSHIFT, SDLK_RETURN, SDLK_RIGHT, SDLK_LEFT, SDLK_UP, SDLK_DOWN, SDLK_c, SDLK_d, SDLK_s,
	SDLK_a, (SDL_KeyCode) NULL
};

ConfigEntry PlatformConfigFile[] = {
	{"sdl_default_scale", 0, &default_scale, 1, NULL, 0},
	{"sdl_fullscreen", 0, &fullscreen, 0, NULL, 0},
	{"sdl_use_framelimit", 0, &use_framelimit, 0, NULL, 0},
	{"sdl_use_vsync", 0, &use_vsync, 1, NULL, 0},
	{"sdl_key_a", 0, &keymap[0], keymap[0], NULL, 0},
	{"sdl_key_b", 0, &keymap[1], keymap[1], NULL, 0},
	{"sdl_key_select", 0, &keymap[2], keymap[2], NULL, 0},
	{"sdl_key_start", 0, &keymap[3], keymap[3], NULL, 0},
	{"sdl_key_right", 0, &keymap[4], keymap[4], NULL, 0},
	{"sdl_key_left", 0, &keymap[5], keymap[5], NULL, 0},
	{"sdl_key_up", 0, &keymap[6], keymap[6], NULL, 0},
	{"sdl_key_down", 0, &keymap[7], keymap[7], NULL, 0},
	{"sdl_key_r", 0, &keymap[8], keymap[8], NULL, 0},
	{"sdl_key_l", 0, &keymap[9], keymap[9], NULL, 0},
	{"sdl_key_x", 0, &keymap[10], keymap[10], NULL, 0},
	{"sdl_key_y", 0, &keymap[11], keymap[11], NULL, 0},
	{"", -1, NULL, 0, NULL, 0}
};
}
