#pragma once

#include <Arduino.h>

namespace PersistentStorage {
  bool setup(size_t size);
  bool loop();

  bool registerPartition(const char* partitionName, size_t size);
  bool readPartition(const char* partitionName, uint32_t address, byte* data, size_t size);
  bool writePartition(const char* partitionName, uint32_t address, byte* data, size_t size);
  bool commit(const char* partitionName);
};