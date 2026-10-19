"""Enable LTO with one consistent pinned M5GFX class definition."""
from pathlib import Path

Import("env")

# M5GFX.h includes the optional filesystem mixin before LGFXBase.hpp, while
# several M5GFX implementation units include LGFXBase.hpp directly. With LTO
# those paths otherwise define LovyanGFX with different bases. Normalize the
# documented include order for every C++ unit, including library/core units.
# This changes no dependency files and keeps the normal filesystem overloads.
if not env.get("CARDPUTER_GFX_INCLUDE_NORMALIZED", False):
    framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    gfx = Path(env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV/M5GFX/src"))
    fs = framework / "libraries/FS/src"
    env.PrependUnique(CPPPATH=[str(gfx), str(fs),
                              str(framework / "libraries/SPI/src"),
                              str(framework / "libraries/Wire/src")])
    env.Append(CXXFLAGS=["-include", str(fs / "FS.h"), "-include", str(gfx / "M5GFX.h")])
    env["CARDPUTER_GFX_INCLUDE_NORMALIZED"] = True

# The platform adds -fno-lto between the hooks. Remove both old LTO choices
# before appending the final enabled setting, so the last driver option wins.
# AppendUnique alone would retain an earlier -flto before the later -fno-lto.
env["LINKFLAGS"] = [flag for flag in env.get("LINKFLAGS", [])
                    if flag not in ("-flto", "-fno-lto", "-fuse-linker-plugin")]
env.AppendUnique(LINKFLAGS=["-flto", "-fuse-linker-plugin"])
