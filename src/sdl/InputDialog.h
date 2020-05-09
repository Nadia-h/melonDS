#ifndef __SDL_INPUTDIALOG_H
#define __SDL_INPUTDIALOG_H

#include <mutex>
#include <SDL2/SDL.h>
#include "../types.h"

class InputDialog {
	SDL_Window* window;
	SDL_Renderer* rend;
	SDL_Texture* text_texture;
	int curr_key;
	bool done;
	auto create_text(const char* text) -> SDL_Texture*;
public:
	InputDialog();
	~InputDialog();
	auto run() -> void;
	auto key(SDL_Keycode code) -> void;
	auto is_done() -> bool;
};

#endif
