#include "NvsErasePolicy.h"
#include <cassert>
#include <cstdio>

int main() {
  assert(nativeBlockSharedNvsFullErase(true, "nvs", 0x5000, 0, 0x5000));
  assert(!nativeBlockSharedNvsFullErase(true, "nvs", 0x5000, 0, 0x1000));
  assert(!nativeBlockSharedNvsFullErase(true, "nvs", 0x5000, 0x1000, 0x1000));
  assert(!nativeBlockSharedNvsFullErase(true, "nvs", 0x5000, 1, 0x5000));
  assert(!nativeBlockSharedNvsFullErase(true, "nvs", 0x5000, 0, 0x4fff));
  assert(!nativeBlockSharedNvsFullErase(true, "nvs", 0x5000, 0, 0x5001));
  assert(!nativeBlockSharedNvsFullErase(true, "otadata", 0x2000, 0, 0x2000));
  assert(!nativeBlockSharedNvsFullErase(false, "nvs", 0x5000, 0, 0x5000));
  assert(!nativeBlockSharedNvsFullErase(true, "another_nvs", 0x5000, 0, 0x5000));
  assert(!nativeBlockSharedNvsFullErase(true, nullptr, 0x5000, 0, 0x5000));
  std::puts("Shared NVS full-erase guard / normal GC tests passed");
}
