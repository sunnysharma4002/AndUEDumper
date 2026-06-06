# Android Unreal Engine Dumper / UE Dumper

Generate sdk and functions script for unreal engine games on android.

The dumper is based on [UE4Dumper-4.25](https://github.com/guttir14/UnrealDumper-4.25)
project.

## Features

* Supported ABI ARM64, ARM, x86 and x86_64
* Can be compiled as executable for external and as library for internal use
* Dump UE offsets, classes, structs, enums and functions
* Dump SDK using GUObjectArray or GWorld from the executable
* Dump strings, object list and actors from the executable
* Generate function names json script to use with IDA & Ghidra etc
* Symbol and pattern scanning to find GUObjectArray, GNames and NamePoolData addresses automatically
* Find GEngine and GWorld in Data segment
* Find ProcessEvent index and offset (64bit only for now)
* Dump UE library from memory
* BGMI profile with direct GNames, GUObjectArray and GWorld offsets

## Currently Supported Games

* Arena Breakout
* Ark Ultimate
* Auroria
* BGMI
* Black Clover M
* Blade Soul Revolution
* Case 2 Animatronics
* Century Age of Ashes
* Delta Force
* Dislyte
* Farlight 84
* Hello Neighbor
* Hello Neighbor Nicky's Diaries
* Injustice 2
* King Arthur Legends Rise
* Lineage 2 Revolution
* Lineage W
* Mortal Kombat
* Night Crows
* Odin Valhalla Rising
* PUBG
* RL Sideswipe
* Real Boxing 2
* Rooftops Parkour Pro
* Special Forces Group 2
* The Baby In Yellow
* Torchlight: Infinite
* Tower Of Fantasy
* Wuthering Waves
* eFootball (PES)

## Library Usage

Simply load or inject the library with whichever method and let it do it's thing.
Run logcat with tag filter "UEDump3r" for dump logs.
The dump output will be at the game's external data folder (/sdcard/Android/data/< game >/files) to avoid external storage permission.

## Executable Usage

You will have to push the dumper in an executable directory like /data/local/tmp then give it execute permission. Its recommended to have adb, you can check [push](AndUEDumper/push.bat) script for this.
Use the compatible dumper, if game is 64bit use arm64 or x86_64, if 32bit then use arm or x86 version.

```bash
Usage: ./UEDump3r [-h] -o <outputPath> [ options ]

Required arguments:
   -o, --output <path>                 File output path.

Optional arguments:
   -h, --help                         Display help.
   -p, --package <packageName>         Package name of app.
   -m, --mem <1|2>                     Memory access type. 1 = process_vm_readv, 2 = pread.

SDK/Object/String arguments:
   --sdku                              Dump SDK with GUObjectArray.
   --sdkw                              Dump SDK with GWorld.
   --strings                           Dump strings.
   --objs                              Dump object list.
   --actors                            Show actors with GWorld.
   --gname <address>                   GNames pointer address.
   --guobj <address>                   GUObject pointer address.
   --gworld <address>                  GWorld pointer address.

Lib dump arguments:
   -d, --dump                          Dump UE library from memory.
   --lib                               Dump libUE4.so from memory.
   --raw                               Output raw lib and not rebuild it.
   --fast                              Enable fast dumping.

Other arguments:
   --newue                             Run in UE 4.23+ mode.
   --ptrdec                            Use pointer decryption mode.
   --verbose                           Show verbose output.
   --derefgname <true/false>           De-reference GNames address.
   --derefguobj <true/false>           De-reference GUObject address.
```

### BGMI Executable Usage

This build includes a BGMI profile for `com.pubg.imobile`. The profile has built-in offsets for:

```text
GNames raw source: 0x9BFB714
GNames direct:     0x9BFB714 + 0x88
GUObjectArray:     0x9DE3114
GWorld:            0x9FEB994 + 0x3C
```

For BGMI names, the dumper resolves the working names table internally with:

```text
raw 0x9BFB714 -> [[raw] + 0x88]
```

The normal BGMI SDK dump command is:

```bash
/data/local/tmp/UEDump3r --package com.pubg.imobile --output /sdcard --sdku --lib
```

The package can be omitted when using the manual executable dump modes because BGMI is selected by default:

```bash
/data/local/tmp/UEDump3r --output /sdcard --sdku --lib
```

If BGMI updates and only the offsets change, override them from the CLI:

```bash
/data/local/tmp/UEDump3r --output /sdcard --package com.pubg.imobile --sdku --gname 0x9BFB79C --guobj 0x9DE3114
```

For BGMI, `--newue`, `--derefgname` and `--derefguobj` are accepted for CLI compatibility but ignored by the BGMI profile.

## Output Files

### AIOHeader.hpp

* An all-in-one dump file header

### SDK.txt

* Text copy of the all-in-one SDK dump

### SDK/*.hpp

* Per-package SDK headers

### Offsets.hpp

* Header containing UE Offsets

### Logs.txt

* Log file containing dump process logs

### Objects.txt

* ObjObjects dump

### script.json

* If you are familiar with Il2cppDumper script.json, this is similar. It contains a json array of function names and addresses

## Adding a new game to the Dumper

Follow the prototype in [GameProfiles](AndUEDumper/src/UE/UEGameProfiles)

You can also use the provided patterns to find GUObjectArray, GNames or NamePoolData.
For games with custom pointer chains, direct offsets or extra generated offset headers, follow the BGMI profile in [BGMI.hpp](AndUEDumper/src/UE/UEGameProfiles/BGMI.hpp).

## Building

You need to have 'make' installed on your OS and NDK_HOME env variable should be set.

```bash
git clone --recursive https://github.com/sharmaofficiall/AndUEDumper
cd AndUEDumper/AndUEDumper
make clean && make
```

On Windows, you can also build from the `AndUEDumper` folder with:

```bat
build.bat
```

Choose `ndk-build` to produce binaries under `ndk_build\libs`.

## TODO

* Sort Generated Packages & Solve Dependencies
* [Dumper-7](https://github.com/Encryqed/Dumper-7) Auto Find Offsets

## Credits & Thanks

* [UE4Dumper-4.25](https://github.com/guttir14/UnrealDumper-4.25)
* [Il2cppDumper](https://github.com/Perfare/Il2CppDumper)
* [Dumper-7](https://github.com/Encryqed/Dumper-7)
* [UEDumper](https://github.com/Spuckwaffel/UEDumper)
