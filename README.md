This is the source of my melonDS switch version.

It's a mess and not finished. Over the next time, I want to integrate the ARM Neon code(of course not as it's the current state!) into melonDS, maybe the Android port benefits from this too. The ARM64 JIT has already been included in the JIT branch of the melonDS main repository.

### Install Instructions for manjaro on Pinebook Pro:
- `sudo pacman -S sdl2 sdl2_gfx sdl2_ttf`
- `git clone https://github.com/nadiaholmquist/melonDS.git`
- `cd melonDS` 
- `git checkout pbp` 
- `mkdir -p build` 
- `cd build`
- `CFLAGS="-march=native -mcpu=native" CXXFLAGS="-march=native -mcpu=native" cmake .. -DBUILD_LIBUI=OFF` 
- `make -j6`

### Post Install file modifications:
- The executable file path is `melonDS/build/melonDS-sdl`
- Before running melonDS you will need the Nintendo DS bios and firmware files.
- They need to be named `bios7.bin` `bios9.bin` and `firmware.bin`
- Place the 3 files in `~/.config/melonDS-sdl`
- Copy the `romlist.bin` file into `~/.config/melonDS-sdl`
- now run in the build folder`./melonDS-sdl` with the path to your ROM file.

### Credits:
- Arisotura, obviously for melonDS
- Dolphin people, of whom I've taken the JIT code emitter and who helped me on IRC!
- Hydr8gon, who did the original melonDS switch port (from which this ports borrows quite a lot of code)
- [dear imgui](https://github.com/ocornut/imgui) and [dr_wav](https://github.com/mackron/dr_libs)
- endrift, the cmake switch buildscript is based of the of mgba
- libnx and devkitpro people
