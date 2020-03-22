#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <condition_variable>

#include "font8x8_basic.h"
#include "InputDialog.h"
#include "PlatformConfig.h"

struct key_naming {
	const char* name;
	const u32 num;
};

const key_naming key_order[] {
	{"A", 0}, {"B", 1}, {"X", 10}, {"Y", 11}, {"L", 9}, {"R", 8}, {"SELECT", 2},
	{"START", 3}, {"Up", 6}, {"Down", 7}, {"Left", 5}, {"Right", 4}
};

InputDialog::InputDialog() :
	window(), rend(), curr_key(-1), text_texture(), done()
{
	window = SDL_CreateWindow(
			"melonDS input configuration",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 500, 100,
			SDL_WINDOW_HIDDEN
	);
	
	rend = SDL_CreateRenderer(window, -1, 0);
	SDL_RenderSetLogicalSize(rend, 250, 50);
	key(0);
	SDL_ShowWindow(window);
}

InputDialog::~InputDialog() {
	SDL_DestroyTexture(text_texture);
	SDL_DestroyRenderer(rend);
	SDL_DestroyWindow(window);
}

auto InputDialog::key(SDL_Keycode code) -> void {
	if (curr_key > -1)
		Config::keymap[key_order[curr_key].num] = code;

	curr_key++;
	if (curr_key == SDL_arraysize(key_order)) {
		done = true;
		return;
	}

	if (text_texture != nullptr)
		SDL_DestroyTexture(text_texture);
	std::string newtext = "Press a key for ";
	newtext += key_order[curr_key].name;

	text_texture = create_text(newtext.c_str());
}

auto InputDialog::run() -> void {
	SDL_SetRenderDrawColor(rend, 0, 0, 0, 0);
	SDL_RenderClear(rend);

	int w, h;
	SDL_QueryTexture(text_texture, NULL, NULL, &w, &h);
	SDL_Rect r, r2;
	r.w = w; r.h = h; r.x = 0; r.y = 0;
	r2.w = w; r2.h = h; r2.x = (125 - (w / 2)); r2.y = (25 - (h / 2));

	SDL_RenderCopy(rend, text_texture, &r, &r2);
	SDL_RenderPresent(rend);
}

// This should probably go somewhere else eventually
#define FONT_WIDTH 8
#define FONT_HEIGHT 8

auto InputDialog::create_text(const char* text) -> SDL_Texture* {
	auto len = strlen(text);
	int row_width = len * FONT_WIDTH;
	auto tex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, row_width, FONT_HEIGHT);
	u32* t;
	int stride;
	SDL_LockTexture(tex, NULL, (void**) &t, &stride);

	for (int i = 0; i < len; i++) {
		auto offset = i * FONT_WIDTH;
		char ch = text[i];
		if (ch > 127) ch = 0;
		
		for (int j = 0; j < FONT_HEIGHT; j++) {
			auto row = row_width * j;

			auto cr = font8x8_basic[ch][j];
			for (int k = 7; k > -1; k--) {
				auto f = (cr >> k & 1) * (255 - ((127 / FONT_HEIGHT) * j));
				t[row + offset + k] = (f << 24) | (f << 16) | (f << 8) | f;
			}
		}
	}

	SDL_UnlockTexture(tex);
	return tex;
}

auto InputDialog::is_done() -> bool {
	return done;
}
