#include "FactoryFirmware.h"
#include "NvsErasePolicy.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <cstring>

namespace {
uint32_t blockedNvsErases = 0;
const esp_partition_t* factoryPartition() {
  const auto* part = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
      ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory");
  if (!part || part->address != 0x340000 || part->size != 0x400000) return nullptr;
  return part;
}
}

FactoryFirmwareState cardputerFactoryFirmwareState() {
  FactoryFirmwareState state;
  const auto* part = factoryPartition();
  if (!part) return state;
  state.address = part->address;
  esp_app_desc_t description{};
  state.error = esp_ota_get_partition_description(part, &description);
  if (state.error != ESP_OK) return state;
  if (std::strncmp(description.project_name, "cardputer-adv", sizeof(description.project_name)) != 0) {
    state.error = ESP_ERR_INVALID_VERSION;
    return state;
  }
  std::memcpy(state.version, description.version, sizeof(description.version));
  state.header_found = true;
  return state;
}

esp_err_t cardputerSelectFactoryBoot() {
  const auto state = cardputerFactoryFirmwareState();
  if (!state.header_found) return state.error;
  return esp_ota_set_boot_partition(factoryPartition());
}

bool cardputerFactoryIsNextBoot() {
  const auto* selected = esp_ota_get_boot_partition();
  const auto* factory = factoryPartition();
  return selected && factory && selected->address == factory->address;
}

uint32_t cardputerBlockedNvsEraseCount() { return blockedNvsErases; }

extern "C" esp_err_t __real_esp_partition_erase_range(const esp_partition_t*, size_t, size_t);
extern "C" esp_err_t __wrap_esp_partition_erase_range(
    const esp_partition_t* partition, size_t offset, size_t bytes) {
  if (partition && nativeBlockSharedNvsFullErase(
      partition->type == ESP_PARTITION_TYPE_DATA && partition->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS,
      partition->label, partition->size, offset, bytes)) {
    ++blockedNvsErases;
    return ESP_ERR_NOT_SUPPORTED;
  }
  return __real_esp_partition_erase_range(partition, offset, bytes);
}
