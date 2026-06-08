#include "ble_adv.h"

#include "nvs_store.h"
#include "tag.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "sdkconfig.h"

#include "nimble/nimble_npl.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/ble_gap.h"

#define LOG_TAG "ble_adv"

// Epoch length: how often the tag rotates and the advertised identifier
// changes. Configured via CONFIG_ESPTAG_ROTATE_INTERVAL_MS (esptag
// configuration menu, default 15 min to match the Find My cadence); drop it to
// a few seconds to observe rotation while testing.
#define ROTATE_INTERVAL_MS CONFIG_ESPTAG_ROTATE_INTERVAL_MS

// Tag whose rotating advertising key (p_curr) drives the payload. Retained from
// ble_adv_init; after init it is owned solely by the host task (on_sync and the
// rotation callout both run there), so it needs no locking.
static tag_t *s_tag = NULL;

// Fires on the host task every ROTATE_INTERVAL_MS to advance the epoch.
static struct ble_npl_callout s_rotate_timer;

/* Static helper declaration */

// Host stack reset: the controller dropped sync. NimBLE re-syncs and invokes
// on_sync again, so there is nothing to do here but log the reason.
static void on_reset(int reason);

// Host/controller are in sync and the stack is ready. Starts advertising and
// arms the rotation timer.
static void on_sync(void);

// FreeRTOS host task body. nimble_port_run blocks forever (there is no shutdown
// path); the deinit tail runs only if it ever returns.
static void host_task(void *param);

// Rotation timer callback (runs on the host task): advance the epoch and
// re-advertise under the new identity, then re-arm the timer.
static void on_rotate(struct ble_npl_event *ev);

// Build the 27-byte offline-finding payload from the tag's current p_curr.
static int build_payload(const tag_t *tag, ble_adv_payload_t *out);

// Derive the 6-byte BLE random address from p_curr[0..5]. NimBLE expects the
// address little-endian; the Find My address is p_curr big-endian, so the bytes
// are reversed, then the two MSBs are forced to 0b11 (static random address).
static void build_addr(const tag_t *tag, uint8_t addr[6]);

// Set the random address + advertising data from the current tag state and
// start advertising. Assumes advertising is currently stopped.
static int adv_apply(void);

/* Header implementation */

int ble_adv_init(tag_t *tag) {
    if (tag == NULL) {
        ESP_LOGE(LOG_TAG, "tag is null");
        return 1;
    }
    s_tag = tag;

    ESP_LOGI(LOG_TAG, "initialising BLE advertising (rotate every %d ms)",
             ROTATE_INTERVAL_MS);

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(LOG_TAG, "nimble port init failed: %s", esp_err_to_name(err));
        return 1;
    }
    ESP_LOGI(LOG_TAG, "nimble port initialised, starting host task");

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    // Init the timer here (the default eventq exists after nimble_port_init) so
    // a re-sync after a controller reset re-arms rather than re-inits it.
    ble_npl_callout_init(&s_rotate_timer, nimble_port_get_dflt_eventq(),
                         on_rotate, NULL);

    nimble_port_freertos_init(host_task);
    return 0;
}

/* Static helper implementation */

static void on_reset(int reason) {
    ESP_LOGW(LOG_TAG, "nimble host reset, reason=%d", reason);
}

static void on_sync(void) {
    ESP_LOGI(LOG_TAG, "nimble host synced");

    if (adv_apply() != 0) {
        ESP_LOGE(LOG_TAG, "initial advertising start failed");
        return;
    }

    // Arm the first rotation.
    ble_npl_callout_reset(&s_rotate_timer,
                          ble_npl_time_ms_to_ticks32(ROTATE_INTERVAL_MS));
    ESP_LOGI(LOG_TAG, "rotation armed, first epoch in %d ms", ROTATE_INTERVAL_MS);
}

static void host_task(void *param) {
    ESP_LOGI(LOG_TAG, "nimble host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_rotate(struct ble_npl_event *ev) {
    ESP_LOGI(LOG_TAG, "epoch elapsed, rotating from counter=%lu",
             (unsigned long)s_tag->counter);

    // The random address and advertising data can only change while stopped.
    // BLE_HS_EALREADY just means advertising was not running, not an error.
    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(LOG_TAG, "adv stop returned %d", rc);
    }

    if (tag_rotate(s_tag) != 0) {
        ESP_LOGE(LOG_TAG, "tag rotate failed");
        // Fall through: re-advertise under the unchanged identity rather than
        // going dark, and try again next epoch.
    }
#ifdef CONFIG_ESPTAG_PERSIST_COUNTER
    else if (nvs_store_save_counter(s_tag->counter) != 0) {
        // Persist failure is non-fatal: keep advertising under the new identity
        // and retry the write next epoch. The worst case is replaying from an
        // earlier counter after a reboot, not going dark.
        ESP_LOGW(LOG_TAG, "counter persist failed");
    }
#endif // CONFIG_ESPTAG_PERSIST_COUNTER

    if (adv_apply() != 0) {
        ESP_LOGE(LOG_TAG, "re-advertising failed");
    }

    ble_npl_callout_reset(&s_rotate_timer,
                          ble_npl_time_ms_to_ticks32(ROTATE_INTERVAL_MS));
}

static int build_payload(const tag_t *tag, ble_adv_payload_t *out) {
    // No null checks: this is a file-static helper called only with s_tag (a
    // module invariant, set once in ble_adv_init) and a stack-local out. The
    // internal helpers (adv_apply, on_rotate, build_addr) follow the same
    // convention and deref s_tag directly.
    out->of_type = 0x12;
    out->of_len = 25;
    out->status = (uint8_t)esp_random();
    memcpy(out->key_mid, &tag->p_curr[6], sizeof(out->key_mid));
    out->key_hi = tag->p_curr[0] >> 6;
    out->hint = 0;
    return 0;
}

static void build_addr(const tag_t *tag, uint8_t addr[6]) {
    for (int i = 0; i < 6; i++) {
        addr[i] = tag->p_curr[5 - i];
    }
    addr[5] |= 0xC0;  // top two bits = 0b11: static random address
}

static int adv_apply(void) {
    uint8_t addr[6];
    build_addr(s_tag, addr);
    int rc = ble_hs_id_set_rnd(addr);
    if (rc != 0) {
        ESP_LOGE(LOG_TAG, "set random address failed: %d", rc);
        return 1;
    }

    // Full advertising data: 0x1e length, 0xff manufacturer-specific, 0x4c 0x00
    // Apple company id, then the 27-byte offline-finding payload = 31 bytes.
    uint8_t data[4 + BLE_ADV_PAYLOAD_LEN] = {0x1e, 0xff, 0x4c, 0x00};
    ble_adv_payload_t pl;
    if (build_payload(s_tag, &pl) != 0) {
        return 1;
    }
    memcpy(&data[4], &pl, sizeof(pl));

    rc = ble_gap_adv_set_data(data, sizeof(data));
    if (rc != 0) {
        ESP_LOGE(LOG_TAG, "set adv data failed: %d", rc);
        return 1;
    }

    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_NON,
        .disc_mode = BLE_GAP_DISC_MODE_NON,
    };
    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &params,
                           NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(LOG_TAG, "adv start failed: %d", rc);
        return 1;
    }

    // Log the address MSB-first (the order a scanner displays it) so it can be
    // matched against the scan; counter ties it to the current epoch.
    ESP_LOGI(LOG_TAG,
             "advertising as %02X:%02X:%02X:%02X:%02X:%02X (counter=%lu)",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0],
             (unsigned long)s_tag->counter);
    ESP_LOG_BUFFER_HEX_LEVEL(LOG_TAG, data, sizeof(data), ESP_LOG_DEBUG);
    return 0;
}

