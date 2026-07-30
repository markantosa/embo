#include "uas_sensor.h"
#include "config.h"
#include "../ble/ble_service.h"

#include "driver/spi_master.h"
#include "esp_adc/adc_oneshot.h"

// ---- AD9833 DDS, direct driver/spi_master.h (write-only, no MISO/readback
// register — replaces the Arduino-only AD9833 library) ----
//
// AD9833 on GPIO38 (FSYNC/CS), SPI Mode 2. Shares MOSI/CLK (35/36) with the
// TFT on a different CS line. LovyanGFX's Bus_SPI claims SPI2_HOST directly,
// so the AD9833 gets its own dedicated host, SPI3_HOST, to avoid contention.
//
// Protocol (AD9833 datasheet): 16-bit control/data words, MSB-first, framed
// by FSYNC low for the word. B28 mode loads a 28-bit frequency word as two
// consecutive 14-bit writes (LSBs then MSBs). Assumes the common 25MHz
// module crystal (MCLK) used on AD9833 breakout boards.
static constexpr double AD9833_MCLK_HZ = 25000000.0;

static constexpr uint16_t AD9833_CTRL_B28   = (1u << 13);
static constexpr uint16_t AD9833_CTRL_RESET = (1u << 8);
static constexpr uint16_t AD9833_FREQ0_ADDR = (0x1u << 14); // 01 -------------- (FREQ0)
static constexpr uint16_t AD9833_PHASE0_ADDR = (0x6u << 13); // 110 ------------- (PHASE0)

static spi_device_handle_t s_ddsSpi = nullptr;
static bool _ddsReady = false;

static bool ddsWriteWord(uint16_t word) {
	spi_transaction_t t = {};
	t.length = 16;
	// AD9833 is MSB-first; spi_master sends tx_data MSB-first by bit order
	// within each byte, so pack the 16-bit word big-endian into tx_data.
	t.tx_data[0] = (uint8_t)(word >> 8);
	t.tx_data[1] = (uint8_t)(word & 0xFF);
	t.flags = SPI_TRANS_USE_TXDATA;
	return spi_device_transmit(s_ddsSpi, &t) == ESP_OK;
}

static void ddsSetFrequency(double freqHz) {
	uint32_t freqReg = (uint32_t)((freqHz * 268435456.0 / AD9833_MCLK_HZ)) & 0x0FFFFFFFu;
	uint16_t lsb = AD9833_FREQ0_ADDR | (uint16_t)(freqReg & 0x3FFF);
	uint16_t msb = AD9833_FREQ0_ADDR | (uint16_t)((freqReg >> 14) & 0x3FFF);
	ddsWriteWord(lsb);
	ddsWriteWord(msb);
}

static void ddsInit(double freqHz) {
	spi_bus_config_t busCfg = {};
	busCfg.mosi_io_num = PIN_SPI_MOSI;
	busCfg.miso_io_num = -1; // no MISO: write-only, no readback register
	busCfg.sclk_io_num = PIN_SPI_CLK;
	busCfg.quadwp_io_num = -1;
	busCfg.quadhd_io_num = -1;
	busCfg.max_transfer_sz = 4;
	spi_bus_initialize(SPI3_HOST, &busCfg, SPI_DMA_DISABLED);

	spi_device_interface_config_t devCfg = {};
	devCfg.mode = 2; // SPI Mode 2 (CPOL=1, CPHA=0), per AD9833 datasheet
	devCfg.clock_speed_hz = (int)SPI_CLK_AD9833;
	devCfg.spics_io_num = PIN_AD9833_CS;
	devCfg.queue_size = 1;
	spi_bus_add_device(SPI3_HOST, &devCfg, &s_ddsSpi);

	// Reset while loading FREQ0/PHASE0, B28 mode (two 14-bit writes per
	// frequency load), then release reset selecting sine output.
	ddsWriteWord(AD9833_CTRL_B28 | AD9833_CTRL_RESET);
	ddsSetFrequency(freqHz);
	ddsWriteWord(AD9833_PHASE0_ADDR | 0); // phase = 0
	ddsWriteWord(AD9833_CTRL_B28); // reset=0, MODE=0/OPBITEN=0 -> sine wave out
}

// ---- UAS envelope ADC (ADC1, 12-bit, 11dB atten -> full 0-3.3V range) ----
static adc_oneshot_unit_handle_t s_adcHandle = nullptr;
static adc_channel_t s_adcChannel = ADC_CHANNEL_0; // GPIO1 == ADC1_CH0 on ESP32-S3

void uasSensorInit() {
	adc_oneshot_unit_init_cfg_t initCfg = {};
	initCfg.unit_id = ADC_UNIT_1;
	adc_oneshot_new_unit(&initCfg, &s_adcHandle);

	adc_oneshot_chan_cfg_t chanCfg = {};
	chanCfg.bitwidth = ADC_BITWIDTH_12;
	chanCfg.atten = ADC_ATTEN_DB_12; // full 0-3.3V range (IDF's replacement for the old ADC_11db)
	adc_oneshot_config_channel(s_adcHandle, s_adcChannel, &chanCfg);

	ddsInit(UAS_DDS_FREQ_HZ);
	_ddsReady = true;

	// Let the signal chain (transducer, envelope detector RC tau=100us)
	// settle before anything reads the ADC.
	delay(UAS_DDS_SETTLE_MS);

	bleLog("UAS: AD9833 init OK, %.0f Hz tone", UAS_DDS_FREQ_HZ);
}

bool uasDdsReady() {
	return _ddsReady;
}

float uasReadEnvelope() {
	uint32_t acc = 0;
	for (uint32_t i = 0; i < UAS_OVERSAMPLE_COUNT; i++) {
		int raw = 0;
		adc_oneshot_read(s_adcHandle, s_adcChannel, &raw);
		acc += (uint32_t)raw;
	}
	float raw = (float)acc / UAS_OVERSAMPLE_COUNT;
	return raw * 3.3f / 4095.0f;
}
