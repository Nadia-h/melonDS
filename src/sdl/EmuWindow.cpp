
#include <SDL2/SDL.h>

#include "../types.h"
#include "EmuWindow.h"

EmuWindow::EmuWindow() :
	window(), gl_window(), rend(), emu_texture()
{
	window = SDL_CreateWindow(
		"melonDS", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		256, 384, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL
	);

	rend = SDL_CreateRenderer(window, 0, SDL_RENDERER_ACCELERATED);

	emu_texture = SDL_CreateTexture(
		rend, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STREAMING,
		256, 384
	);

	SDL_RenderSetLogicalSize(rend, 256, 384);
	SDL_RenderClear(rend);
	SDL_RenderPresent(rend);
}

EmuWindow::~EmuWindow() {
	SDL_DestroyTexture(emu_texture);
	SDL_DestroyRenderer(rend);
	SDL_DestroyWindow(window);
}

void EmuWindow::render() {
	SDL_Rect rect;
	rect.w = 256;
	rect.h = 384;
	rect.x = 0;
	rect.y = 0;

	SDL_RenderClear(rend);
	SDL_RenderCopy(rend, emu_texture, &rect, &rect);
	SDL_RenderPresent(rend);
}

void EmuWindow::update(u32* top, u32* bottom) {
	void* texture;
	int pitch;

	const int fbsize = 256 * 192 * 4;

	SDL_LockTexture(emu_texture, NULL, &texture, &pitch);
	memcpy(texture, top, fbsize);
	memcpy((u8*) texture + fbsize, bottom, fbsize);
	SDL_UnlockTexture(emu_texture);

	this->render();
}

auto EmuWindow::get_window_id() -> u32 {
	return SDL_GetWindowID(window);
}
