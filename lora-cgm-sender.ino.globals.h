#pragma once

#define DATA_COLLECTOR
#define ENABLE_DISPLAY

#define ENABLE_LORA_SENDER
#define ENABLE_LORA_RECEIVER
#if defined(ENABLE_LORA_SENDER) || defined(ENABLE_LORA_RECEIVER)
#define ENABLE_LORA
#endif
