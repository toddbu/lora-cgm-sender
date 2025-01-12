#pragma once

#define DATA_COLLECTOR
#define ENABLE_DISPLAY

#define ENABLE_SYNC_SENDER
#define ENABLE_SYNC_RECEIVER
#if defined(ENABLE_SYNC_SENDER) || defined(ENABLE_SYNC_RECEIVER)
#define ENABLE_SYNC
#endif
