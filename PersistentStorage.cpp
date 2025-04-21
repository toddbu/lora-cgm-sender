#include <SPIFFS.h>
#include "PersistentStorage.h"

Stream* logger = &Serial;

#define PERSISTENT_STORAGE_MAX_PARTITIONS 8
#define MAX_PARtITION_NAME_SIZE 16
struct partition_struct {
  char name[MAX_PARtITION_NAME_SIZE+1];
  uint32_t size;
  uint32_t offset;
  byte* cache;
};

struct partition_struct partitions[PERSISTENT_STORAGE_MAX_PARTITIONS];
size_t partitionCount = 0;
uint32_t nextOffset = 0;

namespace PersistentStorage {
  bool setup(size_t size) {
    if (size > 4096) {
      logger->println("Storage length cannot exceed 4096 bytes");
      return false;
    }

    if (!SPIFFS.begin(true)) {
      logger->println(F("An error has occurred while mounting SPIFFS"));
      return false;
    }

    return true;
  }

  bool loop() {
    return true;
  }

  bool registerPartition(const char* partitionName, size_t size) {
    if (partitionCount >= PERSISTENT_STORAGE_MAX_PARTITIONS) {
      logger->println("No more partitions are available");
      return false;
    }

    struct partition_struct* partition = &partitions[partitionCount];
    int i = 0;
    char* name = partition->name;
    while ((i < MAX_PARtITION_NAME_SIZE) &&
           (*partitionName != '\0')) {
      *name = *partitionName;
      name++;
      partitionName++;
    }
    *name = '\0';
    partition->size = size;
    partition->cache = (byte*) malloc(partition->size);
    if (!partition->cache) {
      logger->println("PersistentStorage cache could not be allocated");
      return false;
    }

    partition->offset = nextOffset;
    nextOffset += partition->size;
    partitionCount++;

    return true;
  }

  int findPartition(const char* partitionName) {
    int i = 0;

    while (i < PERSISTENT_STORAGE_MAX_PARTITIONS) {
      if (strcmp(partitions[i].name, partitionName) == 0) {
        return i;
      }        
    }

    return -1;
  }

  bool readPartition(const char* partitionName, uint32_t address, byte* data, size_t size) {
    int partitionNumber = findPartition(partitionName);
    if (partitionNumber < 0) {
      logger->println("Could not find PersistentStorage partition name");
      return false;
    }

    struct partition_struct* partition = &partitions[partitionNumber];

    if ((address >= partition->size) ||
        ((address + size) > partition->size)) {
      logger->println("Address or size is out of bounds");
      return false;
    }

    byte* target = data;
    byte* source = partition->cache;
    while (size-- > 0) {
      *target = *source;
    }

    return true;
  }

  bool writePartition(const char* partitionName, uint32_t address, byte* data, size_t size) {
    int partitionNumber = findPartition(partitionName);
    if (partitionNumber < 0) {
      logger->println("Could not find PersistentStorage partition name");
      return false;
    }

    struct partition_struct* partition = &partitions[partitionNumber];

    if ((address >= partition->size) ||
        ((address + size) > partition->size)) {
      logger->println("Address or size is out of bounds");
      return false;
    }

    byte* target = partition->cache;
    byte* source = data;
    while (size-- > 0) {
      *target = *source;
    }

    return true;
  }

  bool commit(const char* partitionName) {
  }
}