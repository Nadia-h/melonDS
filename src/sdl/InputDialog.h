#ifndef __SDL_INPUTDIALOG_H
#define __SDL_INPUTDIALOG_H

#include <SDL2/SDL.h>
#include "../types.h"

class InputDialog {
	SDL_Window* window;
	SDL_Renderer* rend;
	auto create_text(const char* text) -> SDL_Texture*;
public:
	InputDialog();
	~InputDialog();
	auto run() -> void;
};

#endif
