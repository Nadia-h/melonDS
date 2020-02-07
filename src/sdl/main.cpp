#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL2_framerate.h>

#include "EmuWindow.h"
#include "ConfPath.h"

#include "../NDS.h"
#include "../GPU.h"
#include "../Config.h"
#include "../SPU.h"

bool running = true;

s32 keymap[][2] = {
	{ SDLK_x,		SDL_CONTROLLER_BUTTON_B },			 // A button
	{ SDLK_z,		SDL_CONTROLLER_BUTTON_A },			 // B button
	{ SDLK_RSHIFT,	SDL_CONTROLLER_BUTTON_BACK },		 // Select button
	{ SDLK_RETURN,	SDL_CONTROLLER_BUTTON_START},		 // Start button
	{ SDLK_RIGHT,	SDL_CONTROLLER_BUTTON_DPAD_RIGHT},	 // D-Pad right
	{ SDLK_LEFT,	SDL_CONTROLLER_BUTTON_DPAD_LEFT},	 // D-pad left
	{ SDLK_UP,		SDL_CONTROLLER_BUTTON_DPAD_UP},		 // D-pad up
	{ SDLK_DOWN,	SDL_CONTROLLER_BUTTON_DPAD_DOWN},	 // D-pad down
	{ SDLK_c,		SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},// R button
	{ SDLK_d,		SDL_CONTROLLER_BUTTON_LEFTSHOULDER}, // L button
	{ SDLK_s,		SDL_CONTROLLER_BUTTON_Y},			 // X button
	{ SDLK_a,		SDL_CONTROLLER_BUTTON_X},			 // Y button
	{ 0, 0 }
};

void audio_callback(void* data, Uint8* stream, int len) {
	SPU::ReadOutput((s16*)stream, len>>2);
}

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <rom>\n", argv[0]);
		return 1;
	}
	
	if (!setup_config_path(NULL)) {
		printf("Could not set up config directory.\n");
		return 1;
	}

	if (SDL_Init(SDL_INIT_EVERYTHING)) {
		printf("SDL initialization failed: %s\n", SDL_GetError());
		return 1;
	}

	auto window = new EmuWindow();

	int arglen = strlen(argv[1]);
	char* sav_name = (char*) malloc(arglen);
	strcpy(sav_name, argv[1]);
	strcpy(sav_name + (arglen - 4), ".sav");

	NDS::Init();

#ifdef JIT_ENABLED
	Config::JIT_Enable = true;
	Config::JIT_MaxBlockSize = 16;
	Config::JIT_BrancheOptimisations = true;
	Config::JIT_LiteralOptimisations = true;
#endif

	GPU3D::InitRenderer(false);
	NDS::LoadROM(argv[1], sav_name, false);

    SDL_AudioDeviceID audio;

    int audio_freq = 48000; // TODO: make configurable?
    SDL_AudioSpec whatIwant, whatIget;
    memset(&whatIwant, 0, sizeof(SDL_AudioSpec));
    whatIwant.freq = audio_freq;
    whatIwant.format = AUDIO_S16LSB;
    whatIwant.channels = 2;
    whatIwant.samples = 1024;
    whatIwant.callback = audio_callback;
	audio = SDL_OpenAudioDevice(NULL, 0, &whatIwant, &whatIget, 0);

	FPSmanager fps;
	SDL_initFramerate(&fps);
	SDL_setFramerate(&fps, 60);

	SDL_Event e;
	u32 keys = 0xFFFF;
	bool touching;
	while (running) {
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
				case SDL_QUIT:
					running = false;
					break;
				case SDL_KEYUP:
				case SDL_KEYDOWN: {
					bool pressed = e.key.state == SDL_PRESSED;
					for (int i = 0; keymap[i][0] != 0; i++) {
						if (keymap[i][0] == e.key.keysym.sym) {
							if (!pressed) keys |= (1 << i);
							else keys &= ~(1 << i);
							break;
						}
					}

					break;
				}
				case SDL_MOUSEBUTTONDOWN:
				case SDL_MOUSEBUTTONUP:
					if (e.button.button == SDL_BUTTON_LEFT) {
						if (e.button.state == SDL_PRESSED) {
							s16 x = e.button.x;
							s16 y = e.button.y;

							if (x >= 256 || y < 192 || y >= 384 || x < 0 || y < 0)
								continue;

							NDS::TouchScreen(x, y - 192);
							NDS::PressKey(16+6);

							touching = true;
						} else {
							NDS::ReleaseScreen();
							NDS::ReleaseKey(16+6);

							touching = false;
						}
					}
					break;
				case SDL_MOUSEMOTION:
					if (e.motion.state == SDL_BUTTON_LMASK && touching) {
						s16 x = e.button.x;
						s16 y = e.button.y;

						if (x >= 256 || y < 192 || y >= 384 || x < 0 || y < 0)
							continue;

						NDS::TouchScreen(x, y - 192);
					} else if (e.motion.state != SDL_BUTTON_LMASK && touching) {
						// Just in case
						touching = false;
						NDS::ReleaseScreen();
						NDS::ReleaseKey(16+6);
					}
					break;
			}
		}

		SPU::InitOutput();

		NDS::SetKeyMask(keys);
		NDS::RunFrame();
		auto front = GPU::FrontBuffer;
		window->update(GPU::Framebuffer[front][0], GPU::Framebuffer[front][1]);
		SDL_framerateDelay(&fps);
	}

	
	return 0;
}
