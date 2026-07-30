#include "bus_probe.h"
#include "config.h"
#include "motors.h"
#include "ble/ble_service.h"

#include <driver/rmt_rx.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

static rmt_channel_handle_t rxChannel = nullptr;
static QueueHandle_t rxQueue = nullptr;
static rmt_symbol_word_t rawSymbols[64];
static bool channelEnabled = false;
static const char *initFailStage = nullptr;
static esp_err_t initFailErr = ESP_OK;

static bool IRAM_ATTR onRmtRxDone(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *userCtx) {
	QueueHandle_t queue = (QueueHandle_t)userCtx;
	BaseType_t highTaskWakeup = pdFALSE;
	xQueueSendFromISR(queue, edata, &highTaskWakeup);
	return highTaskWakeup == pdTRUE;
}

void busProbeInit() {
	rmt_rx_channel_config_t rxChanConfig = {};
	rxChanConfig.gpio_num = (gpio_num_t)PIN_TMC_UART_RX;
	rxChanConfig.clk_src = RMT_CLK_SRC_DEFAULT;
	rxChanConfig.resolution_hz = 1000000; // 1 tick = 1us — plenty of resolution for 115200 baud (~8.68us/bit)
	rxChanConfig.mem_block_symbols = 64;

	esp_err_t err = rmt_new_rx_channel(&rxChanConfig, &rxChannel);
	if (err != ESP_OK) {
		initFailStage = "rmt_new_rx_channel";
		initFailErr = err;
		bleLog("[PROBE] rmt_new_rx_channel failed: %d", (int)err);
		return;
	}

	rxQueue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));

	rmt_rx_event_callbacks_t cbs = {};
	cbs.on_recv_done = onRmtRxDone;
	err = rmt_rx_register_event_callbacks(rxChannel, &cbs, rxQueue);
	if (err != ESP_OK) {
		initFailStage = "rmt_rx_register_event_callbacks";
		initFailErr = err;
		bleLog("[PROBE] rmt_rx_register_event_callbacks failed: %d", (int)err);
		return;
	}

	err = rmt_enable(rxChannel);
	if (err != ESP_OK) {
		initFailStage = "rmt_enable";
		initFailErr = err;
		bleLog("[PROBE] rmt_enable failed: %d (channel left disabled)", (int)err);
		return;
	}

	channelEnabled = true;
	bleLog("[PROBE] bus probe ready on GPIO%d", PIN_TMC_UART_RX);
}

void busProbeCaptureAndLog() {
	if (!channelEnabled) {
		// Surface the boot-time init failure on every periodic call too, since
		// a client that connects after setup() runs would otherwise never see it.
		bleLog("[PROBE] channel not enabled — init failed at '%s' with err %d",
		       initFailStage ? initFailStage : "unknown", (int)initFailErr);
		return;
	}

	rmt_receive_config_t rxConfig = {};
	rxConfig.signal_range_min_ns = 200;      // ignore glitches shorter than this
	rxConfig.signal_range_max_ns = 3000000;  // >3ms idle = end of capture

	esp_err_t err = rmt_receive(rxChannel, rawSymbols, sizeof(rawSymbols), &rxConfig);
	if (err != ESP_OK) {
		bleLog("[PROBE] rmt_receive arm failed: %d", (int)err);
		return;
	}

	// Generates real UART traffic on the line while the RMT channel is
	// listening — this is the thing whose reply (or lack thereof) we want to see.
	motorM1.testConnection();

	rmt_rx_done_event_data_t rxData;
	if (xQueueReceive(rxQueue, &rxData, pdMS_TO_TICKS(100)) != pdTRUE) {
		bleLog("[PROBE] no edges captured within 100ms (line totally idle)");
		return;
	}

	size_t n = rxData.num_symbols;
	bleLog("[PROBE] %u symbols captured on GPIO%d:", (unsigned)n, PIN_TMC_UART_RX);

	// Each symbol packs two (level, duration) intervals. Print in small
	// batches so each BLE log line stays under the notification size limit.
	char buf[180];
	size_t pos = 0;
	size_t maxSymbols = n < 20 ? n : 20; // cap so we don't spam the log

	for (size_t i = 0; i < maxSymbols; i++) {
		pos += snprintf(buf + pos, sizeof(buf) - pos, "%c%u %c%u ",
		                rawSymbols[i].level0 ? 'H' : 'L', rawSymbols[i].duration0,
		                rawSymbols[i].level1 ? 'H' : 'L', rawSymbols[i].duration1);
		if (pos >= sizeof(buf) - 20) break;
	}
	bleLog("[PROBE] %s%s", buf, (n > maxSymbols) ? " ..." : "");
}
