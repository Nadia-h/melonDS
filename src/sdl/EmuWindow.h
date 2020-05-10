#ifndef __SDL_EMUWINDOW_H
#define __SDL_EMUWINDOW_H

#include <SDL2/SDL.h>
#include "../types.h"

class EmuWindow {
private:
	SDL_Window* window;
	SDL_Renderer* rend;
	SDL_Texture* emu_texture;
	bool fullscreen;

	void render();

public:
	EmuWindow();

	~EmuWindow();

	auto show() -> void;

	auto update(u32* top, u32* bottom) -> void;

	auto get_window_id() -> u32;

	auto get_fullscreen() -> bool;

	auto set_fullscreen(bool) -> void;

	auto get_size(int& w, int& h) -> void;

	auto set_size(u32 w, u32 h) -> void;

	auto get_content_size(int& w, int& h) -> void;

	auto set_integer_size(u32 factor) -> void;

	auto has_focus() -> bool;
};

#endif
