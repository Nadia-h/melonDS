/*
    Copyright 2016-2017 StapleButter

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <SDL2/SDL.h>

#include "../Platform.h"
#include "ConfPath.h"

extern char* conf_path;

#include "Emulator.h"

bool is_stopped;

namespace Platform {


typedef struct {
	SDL_Thread* ID;

	void (* Func)();

} ThreadData;

int ThreadEntry(void* data) {
	ThreadData* thread = (ThreadData*) data;
	thread->Func();
	return 0;
}

void StopEmu() {
	is_stopped = true;
}

FILE* OpenFile(const char* path, const char* mode, bool mustexist) {
	FILE* ret;

	if (mustexist) {
		ret = fopen(path, "rb");
		if (ret) ret = freopen(path, mode, ret);
	} else
		ret = fopen(path, mode);

	return ret;
}

FILE* OpenLocalFile(const char* path, const char* mode) {
	std::string fullpath;
	if (path[0] == '/') {
		// If it's an absolute path, just open that.
		fullpath = std::string(path);
	} else {
		// Check user configuration directory
		fullpath += conf_path;
		fullpath += path;
	}

	return OpenFile(fullpath.c_str(), mode, mode[0] != 'w');
}

// TODO this properly
FILE* OpenDataFile(const char* path) {
	return OpenLocalFile(path, "rb");
}


extern "C" {
typedef void (* MelonThreadFunc)();
int sdl_thread_function(void* data) {
	((MelonThreadFunc) data)();
	return 0;
}
}

void* Thread_Create(MelonThreadFunc func) {
	auto t = SDL_CreateThread(sdl_thread_function, "melonDS core thread", (void*) func);
	return t;
}

void Thread_Free(void* thread) {
	int status;
	SDL_WaitThread((SDL_Thread*) thread, &status);
}

void Thread_Wait(void* thread) {
	int status;
	SDL_WaitThread((SDL_Thread*) thread, &status);
}


void* Semaphore_Create() {
	return SDL_CreateSemaphore(0);
}

void Semaphore_Free(void* sema) {
	SDL_DestroySemaphore((SDL_sem*) sema);
}

void Semaphore_Reset(void* sema) {
	while (SDL_SemTryWait((SDL_sem*) sema) == 0);
}

void Semaphore_Wait(void* sema) {
	SDL_SemWait((SDL_sem*) sema);
}

void Semaphore_Post(void* sema) {
	SDL_SemPost((SDL_sem*) sema);
}

void* GL_GetProcAddress(const char* proc) {
	(void) proc;
	return NULL;
}


// TODO
bool MP_Init() {
	return false;
}

void MP_DeInit() {}

int MP_SendPacket(u8* data, int len) {
	(void) data;
	(void) len;
	return 0;
}

int MP_RecvPacket(u8* data, bool block) {
	(void) data;
	(void) block;
	return 0;
}

bool LAN_Init() {
	return false;
}

void LAN_DeInit() {}

int LAN_SendPacket(u8* data, int len) {
	(void) data;
	(void) len;
	return 0;
}

int LAN_RecvPacket(u8* data) {
	(void) data;
	return 0;
}

}
