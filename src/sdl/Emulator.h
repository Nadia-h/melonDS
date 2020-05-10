#ifndef __SDL_EMULATOR_H
#define __SDL_EMULATOR_H

#include <queue>
#include <mutex>

#include "EmuWindow.h"

// This is a bit ugly but can't think of a better way.
extern bool is_stopped;

class Emulator {
	EmuWindow* window;
	std::queue<SDL_Event> event_queue;
	std::mutex event_mutex;
	std::mutex emu_mutex;

	auto handle_events() -> void;

	u32 keys;
	bool touching;
	bool paused;
public:
	Emulator();

	~Emulator();

	auto load_rom(const char* rom, const char* save, bool direct_boot) -> bool;

	auto load_firmware() -> void;

	auto run_frame() -> void;

	auto is_running() -> bool;

	auto queue_event(SDL_Event e) -> void;

	auto read_audio(s16* data, int samples) -> int;

	auto get_window() -> EmuWindow*;

	auto stop() -> void;

	auto is_paused() -> bool;

	auto set_paused(bool pause) -> void;
};

#endif
