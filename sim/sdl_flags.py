# PlatformIO pre-script for [env:sim]: SDL2's compiler and linker flags from sdl2-config, which
# apt's libsdl2-dev and Homebrew's sdl2 both install, so no install prefix is written down.
Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

env.ParseConfig("sdl2-config --cflags --libs")  # noqa: F821
