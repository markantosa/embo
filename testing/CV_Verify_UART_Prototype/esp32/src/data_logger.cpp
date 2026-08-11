#include "data_logger.h"
#include "ble_debug.h"
#include <LittleFS.h>

static const char *LOG_PATH = "/loadcell.csv";
static File _writeFile;
static File _readFile;
static bool _running = false;

void data_logger_init() {
    // true = format LittleFS if mounting fails (e.g. first boot ever, or
    // a corrupted filesystem) — acceptable here since this partition only
    // ever holds this one disposable log file, nothing precious to lose.
    if (!LittleFS.begin(true)) {
        ble_log("DataLogger: LittleFS mount FAILED");
        return;
    }
    ble_log("DataLogger: LittleFS mounted (%u/%u bytes used)",
            (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
}

void data_logger_start() {
    if (_running) return;
    _writeFile = LittleFS.open(LOG_PATH, "w");  // "w" truncates — fresh file every session
    if (!_writeFile) {
        ble_log("DataLogger: failed to open %s for writing", LOG_PATH);
        return;
    }
    _writeFile.println("millis_ms,grams1,grams2");
    _running = true;
    ble_log("DataLogger: started, writing to %s", LOG_PATH);
}

void data_logger_stop() {
    if (!_running) return;
    _writeFile.close();
    _running = false;
    ble_log("DataLogger: stopped (%u bytes written)", (unsigned)data_logger_file_size());
}

bool data_logger_is_running() {
    return _running;
}

void data_logger_log(uint32_t ms, float grams1, float grams2) {
    if (!_running || !_writeFile) return;
    _writeFile.printf("%lu,%.2f,%.2f\n", (unsigned long)ms, grams1, grams2);
}

void data_logger_clear() {
    if (_running) data_logger_stop();
    LittleFS.remove(LOG_PATH);
    ble_log("DataLogger: cleared");
}

size_t data_logger_file_size() {
    File f = LittleFS.open(LOG_PATH, "r");
    if (!f) return 0;
    size_t sz = f.size();
    f.close();
    return sz;
}

bool data_logger_open_for_read() {
    _readFile = LittleFS.open(LOG_PATH, "r");
    return (bool)_readFile;
}

size_t data_logger_read_chunk(uint8_t *buf, size_t maxLen) {
    if (!_readFile) return 0;
    return _readFile.read(buf, maxLen);
}

void data_logger_close_read() {
    if (_readFile) _readFile.close();
}
