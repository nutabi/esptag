#include "findmy_adv.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/ble_gap.h"

#define LOG_TAG "findmy_adv"

// Length of the manufacturer payload tail (the bytes after `ff 4c 00`).
#define BLE_ADV_PAYLOAD_LEN 27

/**
 * @brief Apple "offline finding" (Find My-style) manufacturer payload.
 *
 * The 27-byte tail that follows the `ff 4c 00` (manufacturer-specific + Apple
 * company id) header inside the BLE advertising data.
 *
 * Derived from the 28-byte advertising key. The leading 6 bytes of the key
 * become the BLE random address (so the identifier rotates with the key), which
 * is why they are not in this payload. The two MSBs of key byte 0 get displaced
 * by the mandatory 0b11 static-random-address prefix, so they are stashed in
 * key_hi instead, letting a scanner reconstruct the full key.
 */
typedef struct __attribute__((packed)) {
    uint8_t of_type;      // byte 0:  0x12 (offline-finding type)
    uint8_t of_len;       // byte 1:  25   (length of remaining OF data)
    uint8_t status;       // byte 2:  random
    uint8_t key_mid[22];  // bytes 3..24:  key[6..27]
    uint8_t key_hi;       // byte 25: key[0] >> 6
    uint8_t hint;         // byte 26: 0
} ble_adv_payload_t;

_Static_assert(sizeof(ble_adv_payload_t) == BLE_ADV_PAYLOAD_LEN,
               "advertising payload must be 27 bytes");

// Caller's ready callback, invoked once on the host task after the first sync.
static void (*s_on_ready)(void) = NULL;

// Advertising interval (ms), captured at init.
static uint32_t s_adv_interval_ms = 0;

// Current advertising key and whether one has been supplied yet. Owned by the
// host task after init (on_sync and findmy_adv_set_key both run there), so no
// locking is needed.
static uint8_t s_key[FINDMY_ADV_KEY_LEN];
static bool s_has_key = false;

// Whether the host stack has synced and is ready to advertise.
static bool s_synced = false;

// Whether on_ready has been delivered yet (it fires only on the first sync, so
// a driver can init/arm a rotation timer there exactly once).
static bool s_ready_called = false;

/* Static helper declaration */

// Host stack reset: the controller dropped sync. NimBLE re-syncs and invokes
// on_sync again, so there is nothing to do here but log the reason.
static void on_reset(int reason);

// Host/controller are in sync and the stack is ready. Advertises any buffered
// key and hands control to the caller's on_ready callback.
static void on_sync(void);

// FreeRTOS host task body. nimble_port_run blocks forever (there is no shutdown
// path); the deinit tail runs only if it ever returns.
static void host_task(void *param);

// Build the 27-byte offline-finding payload from the advertising key.
static void build_payload(const uint8_t key[FINDMY_ADV_KEY_LEN],
                          ble_adv_payload_t *out);

// Derive the 6-byte BLE random address from key[0..5]. NimBLE expects the
// address little-endian; the Find My address is the key big-endian, so the bytes
// are reversed, then the two MSBs are forced to 0b11 (static random address).
static void build_addr(const uint8_t key[FINDMY_ADV_KEY_LEN], uint8_t addr[6]);

// Set the random address + advertising data from s_key and (re)start
// advertising. Stops any in-progress advertising first.
static esp_err_t adv_apply(void);

/* Header implementation */

esp_err_t findmy_adv_init(void (*on_ready)(void), uint32_t adv_interval_ms) {
    s_on_ready = on_ready;
    s_adv_interval_ms = adv_interval_ms;

    ESP_LOGI(LOG_TAG, "initialising Find My advertising (interval %lu ms)",
             (unsigned long)adv_interval_ms);

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(LOG_TAG, "nimble port init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(LOG_TAG, "nimble port initialised, starting host task");

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

esp_err_t findmy_adv_set_key(const uint8_t key[FINDMY_ADV_KEY_LEN]) {
    if (key == NULL) {
        ESP_LOGE(LOG_TAG, "key is null");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(s_key, key, FINDMY_ADV_KEY_LEN);
    s_has_key = true;

    // Before sync there is no controller to advertise on; the key is buffered
    // and broadcast from on_sync.
    if (!s_synced) {
        return ESP_OK;
    }
    return adv_apply();
}

/* Static helper implementation */

static void on_reset(int reason) {
    ESP_LOGW(LOG_TAG, "nimble host reset, reason=%d", reason);
}

static void on_sync(void) {
    ESP_LOGI(LOG_TAG, "nimble host synced");
    s_synced = true;

    // Re-advertise whatever key we hold: this also runs after a controller reset
    // re-sync, where the advertising state was lost. On the very first sync no
    // key is buffered yet (the driver supplies it from on_ready below), so this
    // is skipped.
    if (s_has_key && adv_apply() != ESP_OK) {
        ESP_LOGE(LOG_TAG, "advertising start failed");
    }

    // Deliver on_ready exactly once, on the first sync. The driver typically sets
    // the initial key (findmy_adv_set_key, which advertises now that we are
    // synced) and arms its rotation timer here — init-once, so it is safe to set
    // up a ble_npl_callout without re-initing it on every re-sync.
    if (s_on_ready != NULL && !s_ready_called) {
        s_ready_called = true;
        s_on_ready();
    }
}

static void host_task(void *param) {
    ESP_LOGI(LOG_TAG, "nimble host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void build_payload(const uint8_t key[FINDMY_ADV_KEY_LEN],
                          ble_adv_payload_t *out) {
    out->of_type = 0x12;
    out->of_len = 25;
    out->status = (uint8_t)esp_random();
    memcpy(out->key_mid, &key[6], sizeof(out->key_mid));
    out->key_hi = key[0] >> 6;
    out->hint = 0;
}

static void build_addr(const uint8_t key[FINDMY_ADV_KEY_LEN], uint8_t addr[6]) {
    for (int i = 0; i < 6; i++) {
        addr[i] = key[5 - i];
    }
    addr[5] |= 0xC0;  // top two bits = 0b11: static random address
}

static esp_err_t adv_apply(void) {
    // The random address and advertising data can only change while stopped.
    // BLE_HS_EALREADY just means advertising was not running, not an error.
    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(LOG_TAG, "adv stop returned %d", rc);
    }

    uint8_t addr[6];
    build_addr(s_key, addr);
    rc = ble_hs_id_set_rnd(addr);
    if (rc != 0) {
        ESP_LOGE(LOG_TAG, "set random address failed: %d", rc);
        return ESP_FAIL;
    }

    // Full advertising data: 0x1e length, 0xff manufacturer-specific, 0x4c 0x00
    // Apple company id, then the 27-byte offline-finding payload = 31 bytes.
    uint8_t data[4 + BLE_ADV_PAYLOAD_LEN] = {0x1e, 0xff, 0x4c, 0x00};
    ble_adv_payload_t pl;
    build_payload(s_key, &pl);
    memcpy(&data[4], &pl, sizeof(pl));

    rc = ble_gap_adv_set_data(data, sizeof(data));
    if (rc != 0) {
        ESP_LOGE(LOG_TAG, "set adv data failed: %d", rc);
        return ESP_FAIL;
    }

    // Advertise once every s_adv_interval_ms. A single legacy advertising event
    // already transmits the PDU once on each enabled advertising channel
    // (37/38/39), so pinning itvl_min == itvl_max yields exactly one sweep of all
    // three channels per window (plus the mandatory 0-10 ms advDelay the
    // controller adds). BLE_GAP_ADV_ITVL_MS converts ms to the 0.625 ms HCI units.
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_NON,
        .disc_mode = BLE_GAP_DISC_MODE_NON,
        .itvl_min = BLE_GAP_ADV_ITVL_MS(s_adv_interval_ms),
        .itvl_max = BLE_GAP_ADV_ITVL_MS(s_adv_interval_ms),
    };
    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &params,
                           NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(LOG_TAG, "adv start failed: %d", rc);
        return ESP_FAIL;
    }

    // Log the address MSB-first (the order a scanner displays it) so it can be
    // matched against the scan.
    ESP_LOGI(LOG_TAG, "advertising as %02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    ESP_LOG_BUFFER_HEX_LEVEL(LOG_TAG, data, sizeof(data), ESP_LOG_DEBUG);
    return ESP_OK;
}
