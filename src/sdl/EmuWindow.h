#ifndef __SDL_EMUWINDOW_H
#define __SDL_EMUWINDOW_H

#include <SDL2/SDL.h>
#include "../types.h"

class EmuWindow {
private:
	SDL_Window* window;
	SDL_Window* gl_window;
	SDL_Renderer* rend;
	SDL_Texture* emu_texture;

	void render();
public:
	EmuWindow();
	~EmuWindow();
	void update(u32* top, u32* bottom);
	auto get_window_id() -> u32;
};

#endif
