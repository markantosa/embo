#include "ble_service.h"

#include <cstdarg>
#include <cstring>

#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble";

// Distinct UUID base from the v2.51 test firmware so both boards' dashboards
// can never accidentally cross-connect during side-by-side bring-up.
// UUID text: 8f6a100X-3b1a-4e3d-9f2e-6a2c6c9a9a10 (X = 1 service, 2 telemetry,
// 3 motor_cmd, 4 home_cmd, 5 log). BLE_UUID128_INIT wants bytes in wire
// (little-endian) order, i.e. the UUID string's bytes reversed.
static const ble_uuid128_t SERVICE_UUID = BLE_UUID128_INIT(
	0x10, 0x9a, 0x9a, 0x6c, 0x2c, 0x6a, 0x2e, 0x9f, 0x3d, 0x4e, 0x1a, 0x3b, 0x01, 0x10, 0x6a, 0x8f);
static const ble_uuid128_t TELEMETRY_UUID = BLE_UUID128_INIT(
	0x10, 0x9a, 0x9a, 0x6c, 0x2c, 0x6a, 0x2e, 0x9f, 0x3d, 0x4e, 0x1a, 0x3b, 0x02, 0x10, 0x6a, 0x8f);
static const ble_uuid128_t MOTOR_CMD_UUID = BLE_UUID128_INIT(
	0x10, 0x9a, 0x9a, 0x6c, 0x2c, 0x6a, 0x2e, 0x9f, 0x3d, 0x4e, 0x1a, 0x3b, 0x03, 0x10, 0x6a, 0x8f);
static const ble_uuid128_t HOME_CMD_UUID = BLE_UUID128_INIT(
	0x10, 0x9a, 0x9a, 0x6c, 0x2c, 0x6a, 0x2e, 0x9f, 0x3d, 0x4e, 0x1a, 0x3b, 0x04, 0x10, 0x6a, 0x8f);
static const ble_uuid128_t LOG_UUID = BLE_UUID128_INIT(
	0x10, 0x9a, 0x9a, 0x6c, 0x2c, 0x6a, 0x2e, 0x9f, 0x3d, 0x4e, 0x1a, 0x3b, 0x05, 0x10, 0x6a, 0x8f);

static uint16_t s_connHandle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_telemetryValHandle = 0;
static uint16_t s_logValHandle = 0;
static bool s_telemetrySubscribed = false;
static bool s_logSubscribed = false;
static volatile bool s_connected = false;

static uint8_t s_telemetryBuf[sizeof(TelemetryPacket)] = {};
static char s_logBuf[200] = {};
static size_t s_logLen = 0;

static MotorCmd s_pendingMotorCmd;
static volatile bool s_motorCmdPending = false;
static volatile uint8_t s_pendingHomeMotorId = 0xFF;
static volatile bool s_homeCmdPending = false;

static int gapEventHandler(struct ble_gap_event *event, void *arg);

static void startAdvertising() {
	ble_gap_adv_params advParams = {};
	ble_hs_adv_fields fields = {};

	fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
	fields.tx_pwr_lvl_is_present = 1;
	fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

	static const char *name = "EMBO-PCB-Test-v3.4";
	fields.name = (uint8_t *)name;
	fields.name_len = strlen(name);
	fields.name_is_complete = 1;

	fields.uuids128 = (ble_uuid128_t *)&SERVICE_UUID;
	fields.num_uuids128 = 1;
	fields.uuids128_is_complete = 1;

	int rc = ble_gap_adv_set_fields(&fields);
	if (rc != 0) {
		ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
		return;
	}

	advParams.conn_mode = BLE_GAP_CONN_MODE_UND;
	advParams.disc_mode = BLE_GAP_DISC_MODE_GEN;
	rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &advParams, gapEventHandler, nullptr);
	if (rc != 0) {
		ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
	}
}

static int gapEventHandler(struct ble_gap_event *event, void *arg) {
	switch (event->type) {
	case BLE_GAP_EVENT_CONNECT:
		if (event->connect.status == 0) {
			s_connHandle = event->connect.conn_handle;
			s_connected = true;
		} else {
			// Failed connection attempt — keep advertising.
			startAdvertising();
		}
		return 0;

	case BLE_GAP_EVENT_DISCONNECT:
		s_connected = false;
		s_connHandle = BLE_HS_CONN_HANDLE_NONE;
		s_telemetrySubscribed = false;
		s_logSubscribed = false;
		startAdvertising(); // allow a second/replacement central to find us
		return 0;

	case BLE_GAP_EVENT_SUBSCRIBE:
		if (event->subscribe.attr_handle == s_telemetryValHandle) {
			s_telemetrySubscribed = event->subscribe.cur_notify;
		} else if (event->subscribe.attr_handle == s_logValHandle) {
			s_logSubscribed = event->subscribe.cur_notify;
		}
		return 0;

	case BLE_GAP_EVENT_MTU:
		ESP_LOGI(TAG, "MTU negotiated: %d", event->mtu.value);
		return 0;

	default:
		return 0;
	}
}

// ---- Characteristic access callbacks ----

static int telemetryAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
	if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
		os_mbuf_append(ctxt->om, s_telemetryBuf, sizeof(s_telemetryBuf));
	}
	return 0;
}

static int logAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg) {
	if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
		os_mbuf_append(ctxt->om, s_logBuf, s_logLen);
	}
	return 0;
}

static int motorCmdAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
	if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
	uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
	if (len != sizeof(MotorCmd)) return 0;
	MotorCmd cmd;
	if (os_mbuf_copydata(ctxt->om, 0, sizeof(cmd), &cmd) != 0) return 0;
	s_pendingMotorCmd = cmd;
	s_motorCmdPending = true;
	return 0;
}

static int homeCmdAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
	if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
	uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
	if (len != 1) return 0;
	uint8_t motorId = 0xFF;
	if (os_mbuf_copydata(ctxt->om, 0, 1, &motorId) != 0) return 0;
	s_pendingHomeMotorId = motorId;
	s_homeCmdPending = true;
	return 0;
}

static const struct ble_gatt_svc_def gattSvcs[] = {
	{
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = (const ble_uuid_t *)&SERVICE_UUID,
		.characteristics = (struct ble_gatt_chr_def[]){
			{
				.uuid = (const ble_uuid_t *)&TELEMETRY_UUID,
				.access_cb = telemetryAccessCb,
				.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
				.val_handle = &s_telemetryValHandle,
			},
			{
				.uuid = (const ble_uuid_t *)&MOTOR_CMD_UUID,
				.access_cb = motorCmdAccessCb,
				.flags = BLE_GATT_CHR_F_WRITE,
			},
			{
				.uuid = (const ble_uuid_t *)&HOME_CMD_UUID,
				.access_cb = homeCmdAccessCb,
				.flags = BLE_GATT_CHR_F_WRITE,
			},
			{
				.uuid = (const ble_uuid_t *)&LOG_UUID,
				.access_cb = logAccessCb,
				.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
				.val_handle = &s_logValHandle,
			},
			{ 0 }, // terminator
		},
	},
	{ 0 }, // terminator
};

static void onSync() {
	ble_hs_id_infer_auto(0, nullptr);
	startAdvertising();
}

static void onReset(int reason) {
	ESP_LOGW(TAG, "NimBLE host reset, reason=%d", reason);
}

static void hostTask(void *param) {
	nimble_port_run();
	nimble_port_freertos_deinit();
}

void bleServiceInit() {
	esp_err_t ret = nimble_port_init();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "nimble_port_init failed: %d", (int)ret);
		return;
	}

	ble_hs_cfg.sync_cb = onSync;
	ble_hs_cfg.reset_cb = onReset;
	ble_hs_cfg.sm_bonding = 0;
	ble_hs_cfg.sm_mitm = 0;
	ble_hs_cfg.sm_sc = 0;

	ble_svc_gap_init();
	ble_svc_gatt_init();

	ble_gatts_count_cfg(gattSvcs);
	ble_gatts_add_svcs(gattSvcs);

	ble_svc_gap_device_name_set("EMBO-PCB-Test-v3.4");

	// Preferred/negotiated ATT MTU — matches the Arduino build's
	// NimBLEDevice::setMTU(247) (more headroom per log-line notification).
	ble_att_set_preferred_mtu(247);

	nimble_port_freertos_init(hostTask);
}

bool bleIsConnected() {
	return s_connected;
}

void bleNotifyTelemetry(const TelemetryPacket &pkt) {
	memcpy(s_telemetryBuf, &pkt, sizeof(pkt));
	if (!s_connected || !s_telemetrySubscribed) return;

	struct os_mbuf *om = ble_hs_mbuf_from_flat(s_telemetryBuf, sizeof(s_telemetryBuf));
	if (!om) return;
	ble_gatts_notify_custom(s_connHandle, s_telemetryValHandle, om);
}

void bleLog(const char *fmt, ...) {
	char buf[200];
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (len < 0) return;
	if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;

	ESP_LOGI(TAG, "%s", buf);

	memcpy(s_logBuf, buf, (size_t)len);
	s_logLen = (size_t)len;

	if (!s_connected || !s_logSubscribed) return;
	struct os_mbuf *om = ble_hs_mbuf_from_flat(s_logBuf, s_logLen);
	if (!om) return;
	ble_gatts_notify_custom(s_connHandle, s_logValHandle, om);
}

bool blePopMotorCmd(MotorCmd &out) {
	if (!s_motorCmdPending) return false;
	noInterrupts();
	out = s_pendingMotorCmd;
	s_motorCmdPending = false;
	interrupts();
	return true;
}

bool blePopHomeCmd(uint8_t &motorIdOut) {
	if (!s_homeCmdPending) return false;
	noInterrupts();
	motorIdOut = s_pendingHomeMotorId;
	s_homeCmdPending = false;
	interrupts();
	return true;
}
