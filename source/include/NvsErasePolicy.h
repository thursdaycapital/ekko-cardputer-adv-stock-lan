#pragma once
#include <cstddef>
#include <cstring>

// A shared NVS failure must not make Arduino's pre-setup recovery format the
// other firmware's accounts. Normal page garbage collection remains enabled.
inline bool nativeBlockSharedNvsFullErase(bool isNvs, const char* label,
                                        size_t partitionSize, size_t offset, size_t bytes) {
  return isNvs && label && std::strcmp(label, "nvs") == 0 && partitionSize > 4096 &&
         offset == 0 && bytes == partitionSize;
}
