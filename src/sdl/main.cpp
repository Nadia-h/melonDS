#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL2_framerate.h>
#include <SDL2/SDL_ttf.h>

#include "EmuWindow.h"
#include "ConfPath.h"
#include "Emulator.h"

#include "PlatformConfig.h"

#include "InputDialog.h"

Emulator* emulator;
InputDialog* input_dialog;

void audio_callback(void* data, Uint8* stream, int len) {
	(void) data;
	emulator->read_audio((s16*)stream, len>>2);
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

	Config::Load();

	emulator = new Emulator();

	if (has_rom) {
		int arglen = strlen(argv[1]);
		char* sav_name = (char*) malloc(arglen);
		strcpy(sav_name, argv[1]);
		strcpy(sav_name + (arglen - 4), ".sav");
		emulator->load_rom(argv[1], sav_name, false);
	} else {
		emulator->load_firmware();
	}

	SDL_Event e;

	//SDL_Thread* emu = SDL_CreateThread(emu_thread, "melonDS emulator thread", NULL);
	FPSmanager fps;
	SDL_initFramerate(&fps);
	SDL_setFramerate(&fps, 60);

	SDL_AudioSpec req;
	req.channels = 2;
	req.format = AUDIO_S16SYS;
	req.freq = 32768;
	req.samples = 1024;
	req.callback = &audio_callback;

	SDL_AudioSpec out;
	SDL_OpenAudio(&req, &out);

	emulator->get_window()->show();

	SDL_PauseAudio(0);

	while (emulator->is_running()) {
		if (input_dialog != nullptr) {
			input_dialog->run();
		}

		while (SDL_PollEvent(&e)) {
			switch (e.type) {
				case SDL_QUIT:
					emulator->stop();
					break;
				case SDL_KEYDOWN:
					if (input_dialog != NULL) {
						input_dialog->key(e.key.keysym.sym);
						if (input_dialog->is_done()) {
							delete input_dialog;
							input_dialog = nullptr;
							emulator->set_pause(false);
						}
						break;
					} else if (e.key.keysym.sym == SDLK_F12) {
						emulator->set_pause(true);
						input_dialog = new InputDialog();
						break;
					}
					continue;
				default: {
					emulator->queue_event(e);
					break;
				}
			}
		}

		emulator->run_frame();
		if (Config::use_framelimit)
			SDL_framerateDelay(&fps);
	}

	delete emulator;

	Config::Save();
	SDL_Quit();

	return 0;
}
