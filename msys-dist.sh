#!/bin/bash

if [[ ! -x melonDS.exe ]]; then
	echo "Run this script from the directory you built melonDS."
	exit 1
fi

mkdir -p dist

executables=(melonDS.exe melonDS-sdl.exe)

for exe in "${executables[@]}"; do
	if [[ -f $exe ]]; then
		for lib in $(ldd $exe | grep mingw | sed "s/.*=> //" | sed "s/(.*)//"); do
			cp "${lib}" dist
		done

		cp $exe dist
	fi
done
cp romlist.bin dist
