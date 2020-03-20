#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL2_framerate.h>

#include "EmuWindow.h"
#include "ConfPath.h"

#include "../NDS.h"
#include "../GPU.h"
#include "../SPI.h"
#include "../Config.h"
#include "../SPU.h"
#include "PlatformConfig.h"

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
	bool has_rom = false;

	if (argc > 1) {
		has_rom = true;
	}
	
	if (!setup_config_path(NULL)) {
		printf("Could not set up config directory.\n");
		return 1;
	}

	if (SDL_Init(SDL_INIT_EVERYTHING)) {
		printf("SDL initialization failed: %s\n", SDL_GetError());
		return 1;
	}


	SDL_AudioSpec req;
	req.channels = 2;
	req.format = AUDIO_S16SYS;
	req.freq = 32768;
	req.samples = 1024;
	req.callback = &audio_callback;

	SDL_AudioSpec out;
	SDL_OpenAudio(&req, &out);

	FPSmanager fps;
	SDL_initFramerate(&fps);
	SDL_setFramerate(&fps, 60);

	Config::Load();

#ifdef JIT_ENABLED
	Config::JIT_Enable = true;
	Config::JIT_MaxBlockSize = 32;
	Config::JIT_BrancheOptimisations = true;
	Config::JIT_LiteralOptimisations = true;
#endif
	Config::Threaded3D = true;

	auto window = new EmuWindow();
	window->set_integer_size(Config::default_scale);

	if (Config::fullscreen) {
		window->set_fullscreen(true);
	}

	NDS::Init();
	GPU3D::InitRenderer(false);

	if (has_rom) {
		int arglen = strlen(argv[1]);
		char* sav_name = (char*) malloc(arglen);
		strcpy(sav_name, argv[1]);
		strcpy(sav_name + (arglen - 4), ".sav");
		NDS::LoadROM(argv[1], sav_name, false);
	} else {
		NDS::LoadBIOS();
	}

	SPU::InitOutput();
	SDL_PauseAudio(0);

	SDL_Event e;
	u32 keys = 0xFFFF;
	bool touching;
	bool should_delay = true;

	window->show();

	while (running) {
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
				case SDL_QUIT:
					running = false;
					break;
				case SDL_KEYUP:
				case SDL_KEYDOWN: {
					bool pressed = e.key.state == SDL_PRESSED;

					if (pressed) {
						switch (e.key.keysym.sym) {
							case SDLK_F11:
								window->set_fullscreen(!window->get_fullscreen());
								break;
							case SDLK_PLUS:
							case SDLK_MINUS: {
								int x, y;
								window->get_content_size(x, y);
								int scale = x / 256;
								int rem = x % 256;
								
								if (e.key.keysym.sym == SDLK_PLUS) {
									window->set_integer_size(scale + 1);
								} else {
									window->set_integer_size(scale + rem == scale ? scale - 1 : scale);
								}
								break;
							}
							case SDLK_SPACE:
								should_delay = !should_delay;
								break;
						}
					}

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
				case SDL_CONTROLLERDEVICEADDED: {
					u32 id = e.cdevice.which;
					SDL_GameController* ct = SDL_GameControllerOpen(id);
					printf("Controller connected: %s\n", SDL_GameControllerName(ct));
					break;
				}
				case SDL_CONTROLLERBUTTONUP:
				case SDL_CONTROLLERBUTTONDOWN: {
					bool pressed = e.cbutton.state == SDL_PRESSED;
					for (int i = 0; keymap[i][0] != 0; i++) {
						if (keymap[i][1] == e.cbutton.button) {
							if (!pressed) keys |= (1 << i);
							else keys &= ~(1 << i);
							break;
						}
					}

					break;
				}
			}
		}

		NDS::SetKeyMask(keys);
		NDS::RunFrame();
		auto front = GPU::FrontBuffer;
		window->update(GPU::Framebuffer[front][0], GPU::Framebuffer[front][1]);
		if (should_delay)
			SDL_framerateDelay(&fps);
	}

	Config::Save();

	return 0;
}
