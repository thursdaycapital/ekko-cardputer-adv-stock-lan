#pragma once
#include <cstdio>

namespace official_recording {
namespace detail {

// One shared creation path for SdBackend and the target-libc regression.
// The adapter is statically bound; production does not keep test hooks/state.
struct NativeRecordingStdio {
  static std::FILE* create(const char* path, const char* mode) { return std::fopen(path, mode); }
};

template<class Stdio = NativeRecordingStdio>
std::FILE* openExclusiveRecordingFile(const char* path) {
  // Pinned newlib supports x -> O_EXCL; FAT prioritizes CREATE_NEW over
  // TRUNCATE. fdopen instead requires an unsupported FAT VFS F_GETFL call.
  // Never retry with plain wb, which could destroy an existing file.
  return Stdio::create(path, "wbx");
}

}  // namespace detail
}  // namespace official_recording
