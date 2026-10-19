#pragma once
#include <esp_err.h>
#include <cstdint>

struct FactoryFirmwareState {
  bool header_found = false;
  uint32_t address = 0;
  char version[33] = {};
  esp_err_t error = ESP_ERR_NOT_FOUND;
};

FactoryFirmwareState cardputerFactoryFirmwareState();
// The SDK verifies the complete target image before changing boot selection.
esp_err_t cardputerSelectFactoryBoot();
bool cardputerFactoryIsNextBoot();
uint32_t cardputerBlockedNvsEraseCount();
