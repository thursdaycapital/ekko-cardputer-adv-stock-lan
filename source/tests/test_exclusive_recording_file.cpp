#include "ExclusiveRecordingFile.h"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
// Replay the exact pinned ESP-IDF/newlib capability gap, without replacing
// SdBackend's shared creation algorithm or faking its filesystem outcomes.
// Hardware/library evidence independently establishes the target's errno 88.
struct TargetFatStdio {
  static unsigned attachCalls, createCalls, closeCalls;
  static int openedFd;
  static int openNew(const char* path) {
    openedFd = ::open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    return openedFd;
  }
  static std::FILE* attach(int, const char*) {
    ++attachCalls; errno = 88; return nullptr;  // Target F_GETFL -> ENOSYS.
  }
  static int closeFd(int fd) { ++closeCalls; return ::close(fd); }
  static std::FILE* create(const char* path, const char* mode) {
    ++createCalls;
    assert(std::strcmp(mode, "wbx") == 0); // No fallback that could truncate.
    return std::fopen(path, mode);
  }
};
unsigned TargetFatStdio::attachCalls = 0, TargetFatStdio::createCalls = 0, TargetFatStdio::closeCalls = 0;
int TargetFatStdio::openedFd = -1;

std::string path(const char* root, const char* name) { return std::string(root) + "/" + name; }
void fixture(const std::string& name, const char* bytes) {
  auto* file = std::fopen(name.c_str(), "wbx"); assert(file);
  const size_t count = std::strlen(bytes);
  assert(std::fwrite(bytes, 1, count, file) == count && std::fclose(file) == 0);
}
void exact(const std::string& name, const char* expected) {
  auto* file = std::fopen(name.c_str(), "rb"); assert(file);
  char bytes[64]{}; const size_t count = std::fread(bytes, 1, sizeof(bytes), file);
  assert(count == std::strlen(expected) && !std::memcmp(bytes, expected, count));
  assert(std::feof(file) && !std::ferror(file) && std::fclose(file) == 0);
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const auto fresh = path(argv[1], "new-marker.NEW");
  errno = 0;
  auto* file = official_recording::detail::openExclusiveRecordingFile<TargetFatStdio>(fresh.c_str());
  const int savedError = errno;
  struct stat info{}; const bool created = ::stat(fresh.c_str(), &info) == 0;
  std::printf("shared SdBackend creation: opened=%d created=%d size=%ld errno=%d fdopen_calls=%u close_calls=%u fopen_calls=%u\n",
              file != nullptr, created, created ? long(info.st_size) : -1L, savedError,
              TargetFatStdio::attachCalls, TargetFatStdio::closeCalls, TargetFatStdio::createCalls);
  std::fflush(stdout);
  if (!file && TargetFatStdio::openedFd >= 0) {
    errno = 0; assert(::fcntl(TargetFatStdio::openedFd, F_GETFD) == -1 && errno == EBADF);
  }
  assert(file); // This is red on the actual old production creation algorithm.
  const char payload[] = "EKKO-CARDPUTER-SD-TEMP-v1\n";
  assert(std::fwrite(payload, 1, sizeof(payload) - 1, file) == sizeof(payload) - 1);
  assert(std::fflush(file) == 0 && ::fsync(::fileno(file)) == 0 && std::fclose(file) == 0);
  exact(fresh, payload);
  assert(TargetFatStdio::attachCalls == 0 && TargetFatStdio::closeCalls == 0 && TargetFatStdio::createCalls == 1);
  for (const char* value : {"", "existing user bytes"}) {
    const auto name = path(argv[1], *value ? "existing-data.NEW" : "existing-empty.NEW");
    fixture(name, value); errno = 0;
    assert(!official_recording::detail::openExclusiveRecordingFile<TargetFatStdio>(name.c_str()) && errno == EEXIST);
    exact(name, value);
  }
  const auto missing = path(argv[1], "missing-parent/fail.NEW"); errno = 0;
  assert(!official_recording::detail::openExclusiveRecordingFile<TargetFatStdio>(missing.c_str()) && errno == ENOENT);
  // Exercise the same native adapter that the production SdBackend uses too.
  const auto native = path(argv[1], "native.NEW");
  file = official_recording::detail::openExclusiveRecordingFile(native.c_str()); assert(file);
  assert(std::fclose(file) == 0);
  assert(!official_recording::detail::openExclusiveRecordingFile(native.c_str()) && errno == EEXIST);
  std::puts("exclusive recording creation passed: target F_GETFL/ENOSYS avoided, existing zero/data preserved, write/sync/close/reopen, native adapter and missing-parent failure");
}
