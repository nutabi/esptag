#ifndef ESPTAG_BLE_ADV
#define ESPTAG_BLE_ADV

#include <stdint.h>

#include "tag.h"

#define BLE_ADV_PAYLOAD_LEN 27

/**
 * Apple "offline finding" (Find My-style) manufacturer payload.
 *
 * This is the 27-byte tail that follows the `ff 4c 00` (manufacturer-specific /
 * Apple company id) header inside the BLE advertising data. Derived from the
 * tag's current public key p_curr: the leading 6 bytes of p_curr become the BLE
 * random address (so the identifier rotates with the key), and the two MSBs of
 * p_curr[0] — displaced by the mandatory 0b11 static-random-address prefix — are
 * stashed in key_hi so a scanner can reconstruct the full key.
 */
typedef struct __attribute__((packed)) {
    uint8_t of_type;      // byte 0:  0x12 (offline-finding type)
    uint8_t of_len;       // byte 1:  25   (length of remaining OF data)
    uint8_t status;       // byte 2:  random
    uint8_t key_mid[22];  // bytes 3..24:  p_curr[6..27]
    uint8_t key_hi;       // byte 25: p_curr[0] >> 6
    uint8_t hint;         // byte 26: 0
} ble_adv_payload_t;

_Static_assert(sizeof(ble_adv_payload_t) == BLE_ADV_PAYLOAD_LEN,
               "advertising payload must be 27 bytes");

/**
 * Bring up the NimBLE stack and spawn the host task.
 *
 * Initialises the NimBLE port (controller + host), wires the sync/reset
 * callbacks, and starts the FreeRTOS host task. The tag pointer is retained
 * (the host task reads p_curr to build the advertising payload and rotates the
 * tag on a timer), so it must outlive the BLE module — pass a struct with
 * static/global lifetime, not a stack local. Once the host syncs, advertising
 * starts and rotates every epoch. Returns nonzero on failure.
 */
int ble_adv_init(tag_t *tag);

/**
 * Stop the host task and tear down the NimBLE stack.
 *
 * Signals the host task to return (it self-deinitialises), then deinitialises
 * the NimBLE port. Returns nonzero on failure.
 */
int ble_adv_deinit(void);

#endif // ESPTAG_BLE_ADV
