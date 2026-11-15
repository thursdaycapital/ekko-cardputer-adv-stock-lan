"""Enable LTO with one consistent pinned M5GFX class definition."""
from pathlib import Path
from platformio.package.manager.library import LibraryPackageManager
from platformio.package.meta import PackageSpec

Import("env")

# M5GFX.h includes the optional filesystem mixin before LGFXBase.hpp, while
# several M5GFX implementation units include LGFXBase.hpp directly. With LTO
# those paths otherwise define LovyanGFX with different bases. Normalize the
# documented include order for every C++ unit, including library/core units.
# This changes no dependency files and keeps the normal filesystem overloads.
if not env.get("CARDPUTER_GFX_INCLUDE_NORMALIZED", False):
    framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    # The library manager may keep multiple transitive versions under names
    # such as M5GFX and M5GFX@<version>. Resolve the project's declaration with
    # that same manager, rather than forcing headers from an arbitrary folder.
    gfx_specs = [PackageSpec(dependency) for dependency in env.GetProjectOption("lib_deps")]
    gfx_specs = [spec for spec in gfx_specs if spec.name and spec.name.lower() == "m5gfx"]
    if len(gfx_specs) != 1:
        raise RuntimeError("Declare exactly one M5GFX dependency for normalized LTO headers")
    gfx_package = LibraryPackageManager(env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV")).get_package(gfx_specs[0])
    if gfx_package is None:
        raise RuntimeError("Install the declared M5GFX dependency before building")
    gfx = Path(gfx_package.path) / "src"
    if not (gfx / "M5GFX.h").is_file():
        raise RuntimeError("The declared M5GFX package is missing its public header")
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
