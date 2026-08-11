#pragma once

#include <stdint.h>
#include <stddef.h>

// Buffers load cell samples to flash (LittleFS) instead of streaming them
// live — decouples data integrity from the live BLE/Serial link being
// perfect for the whole capture window. Capture now, retrieve the
// complete file afterward (see ble_binary_telemetry.h's LOG_CTRL/LOG_DATA
// characteristics for the BLE side of retrieval).
//
// Worth knowing: flash writes have a finite erase/write cycle limit, and
// occasional writes can take longer than the ~1ms typical case (page
// erases especially) — this write happens inside the same 50ms sampling
// loop as the live Serial stream in main.cpp, so a slow write could
// introduce occasional timing jitter in the sample cadence. Not a concern
// for occasional bench sessions; worth knowing if this becomes a
// long-running or very frequent tool.

void data_logger_init();  // mounts LittleFS, call once at boot

void data_logger_start();  // opens the log file fresh (truncates any previous session)
void data_logger_stop();
bool data_logger_is_running();
void data_logger_log(uint32_t ms, float grams1, float grams2);  // no-op unless running

void data_logger_clear();  // deletes the log file entirely
size_t data_logger_file_size();

// Chunked read-back for BLE dump — open, read_chunk() repeatedly until it
// returns 0 (EOF), then close.
bool data_logger_open_for_read();
size_t data_logger_read_chunk(uint8_t *buf, size_t maxLen);
void data_logger_close_read();
