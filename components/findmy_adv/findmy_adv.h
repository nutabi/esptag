#ifndef FINDMY_ADV_H
#define FINDMY_ADV_H

#include <stdint.h>

#include "esp_err.h"

/*
 * findmy_adv — the "link layer" of an Apple "offline finding" (Find My-style)
 * broadcaster.
 *
 * It packages a single 28-byte advertising key into a valid Find My
 * advertisement and puts it on the air, owning the NimBLE stack, the
 * `1e ff 4c 00` manufacturer frame, and the BLE random address. It knows nothing
 * about *why* the key changes — rotating EC identities, arbitrary data payloads,
 * etc. are the caller's concern. The 28-byte key is the link MTU: 6 of those
 * bytes ride in the BLE random address and 22 in the manufacturer payload (plus
 * 2 bits recovered via key_hi), which is why this layer owns the address rather
 * than just the payload tail.
 */

// The advertising key length — the unit findmy_adv broadcasts per frame. 6 bytes
// ride in the BLE random address, 22 in the payload, with the 2 MSBs of byte 0
// recovered from the payload's key_hi field (the address forces them to 0b11).
#define FINDMY_ADV_KEY_LEN 28

/**
 * @brief Bring up the NimBLE stack and spawn the host task.
 *
 * Initialises the NimBLE port (controller + host), wires the sync/reset
 * callbacks, and starts the FreeRTOS host task. Advertising does not begin until
 * a key is supplied (findmy_adv_set_key); a key buffered before sync is
 * broadcast as soon as the stack syncs.
 *
 * @param on_ready        Optional (may be NULL) callback invoked once, on the
 *                        NimBLE host task, when the stack first syncs and is
 *                        ready to advertise. The typical driver sets the initial
 *                        key (findmy_adv_set_key) and arms any rotation timer
 *                        here. Runs on the host task, so it may call
 *                        findmy_adv_set_key directly.
 * @param adv_interval_ms Advertising interval in milliseconds (itvl_min ==
 *                        itvl_max), i.e. one sweep of channels 37/38/39 per
 *                        window.
 * @return ESP_OK on success, or the failing esp_err_t (e.g. from
 *         nimble_port_init).
 */
esp_err_t findmy_adv_init(void (*on_ready)(void), uint32_t adv_interval_ms);

/**
 * @brief Set the advertising key and (re)start advertising under it.
 *
 * Copies the 28-byte key into the module's buffer. If the stack has already
 * synced, advertising is stopped and restarted under the new key/address
 * immediately; otherwise the key is buffered and broadcast on sync.
 *
 * Must be called on the NimBLE host task (e.g. from the on_ready callback or a
 * ble_npl_callout on the default event queue). The module holds no lock: the
 * host task is the sole owner of the advertising state after init.
 *
 * @param key The 28-byte advertising key to broadcast.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on a NULL key, or ESP_FAIL if
 *         the NimBLE GAP calls fail.
 */
esp_err_t findmy_adv_set_key(const uint8_t key[FINDMY_ADV_KEY_LEN]);

#endif // FINDMY_ADV_H
