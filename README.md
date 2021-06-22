# smd_ida_tools2
The IDA Pro tools for the Sega Genesis/MegaDrive romhackers

## Contains
1. ROM files loader
2. Z80 sound drivers loader
3. IDA Pro debugger for Sega Mega Drive/Genesis roms and Z80 sound drivers

## How to compile
1. Edit paths to your IDA/SDK installation according to your real paths
2. Use [VCPKG](https://vcpkg.io/) to install `grpc:x64-windows-static` and `grpc:x86-windows-static`. Use `vcpkg integrate install` to integrate it into Visual Studio
3. Use **Visual Studio 2019** or newer to compile

## How to use
1. Put files from the `loaders` dir into the corresponding IDA folder
2. Put files from the `plugins` dir into the corresponding IDA folder
3. Open ROM in IDA
4. Choose `GensIDA debugger plugin` or `Z80 debugger plugin` debugger
5. Press `F9` to start a debugging process
6. Run `gens_68k.exe` or `gens_z80.exe` from `gens` folder, choose a ROM
7. Debug!
