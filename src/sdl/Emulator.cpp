#include <SDL2/SDL.h>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "Emulator.h"
#include "EmuWindow.h"
#include "PlatformConfig.h"

#include "../NDS.h"
#include "../GPU.h"
#include "../SPU.h"

SDL_GameControllerButton controller_map[] = {
	SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_A,
	SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START,
	SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
	SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
	SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
	SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_X
};

Emulator::Emulator() :
	window(new EmuWindow()), event_queue(), event_mutex(), emu_mutex(), keys(0xFFFF), touching(false), paused(false)
{
	NDS::Init();
	GPU3D::InitRenderer(false);
	SPU::InitOutput();
	window->set_fullscreen(Config::fullscreen);
}

Emulator::~Emulator() {
	delete window;
}

auto Emulator::load_rom(const char* rom, const char* save, bool direct_boot) -> bool {
	is_stopped = !NDS::LoadROM(rom, save, direct_boot);
	return !is_stopped;
}

auto Emulator::load_firmware() -> void {
	NDS::LoadBIOS();
	is_stopped = false;
}

auto Emulator::run_frame() -> void {
	if (is_stopped)
		return;

	handle_events();
	if (!paused) {
		std::unique_lock<std::mutex> lock(emu_mutex);
		NDS::RunFrame();
	}

	auto front = GPU::FrontBuffer;
	window->update(GPU::Framebuffer[front][0], GPU::Framebuffer[front][1]);
}

auto Emulator::handle_events() -> void {
	std::unique_lock<std::mutex> lock(event_mutex);
	SDL_Event e;

	while (event_queue.size() > 0) {
		e = event_queue.front();
		event_queue.pop();
		switch (e.type) {
			case SDL_KEYUP:
			case SDL_KEYDOWN: {
				bool pressed = e.key.state == SDL_PRESSED;

				if (pressed) {
					switch (e.key.keysym.sym) {
						case SDLK_q:
							if (e.key.keysym.mod & KMOD_CTRL) {
								stop();
							}
							break;
						case SDLK_PAUSE:
							set_paused(!is_paused());
							break;
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
							break;
					}
				}

				for (int i = 0; Config::keymap[i] != 0; i++) {
					if (Config::keymap[i] == e.key.keysym.sym) {
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
				int size = sizeof(controller_map) / sizeof(SDL_GameControllerButton);
				for (int i = 0; i < size; i++) {
					if (controller_map[i] == e.cbutton.button) {
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
}

auto Emulator::queue_event(SDL_Event event) -> void {
	std::lock_guard<std::mutex> lock(event_mutex);
	event_queue.push(event);
}

auto Emulator::read_audio(s16* data, int samples) -> int {
	return SPU::ReadOutput(data, samples);
}

auto Emulator::get_window() -> EmuWindow* {
	return window;
}

auto Emulator::is_running() -> bool {
	return !is_stopped;
}

auto Emulator::stop() -> void {
	std::lock_guard<std::mutex> lock(emu_mutex);
	printf("stopping\n");
	NDS::Stop();
}

auto Emulator::is_paused() -> bool {
	return paused;
}

auto Emulator::set_paused(bool paused) -> void {
	this->paused = paused;
}
