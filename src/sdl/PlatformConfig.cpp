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
#include "PlatformConfig.h"

namespace Config
{
	u32 default_scale;
	bool fullscreen;
ConfigEntry PlatformConfigFile[] = {
	{"sdl_default_scale", 0, &default_scale, 1, NULL, 0 },
	{"sdl_fullscreen", 0, &fullscreen, 0, NULL, 0 },
    {"", -1, NULL, 0, NULL, 0}
};

}
