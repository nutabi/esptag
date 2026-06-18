# findmy_adv

A reusable ESP-IDF component that broadcasts a 28-byte advertising key as a valid
Apple **offline-finding** ("Find My"-style) BLE advertisement. It is the *link
layer* of a Find My broadcaster: it frames one key and puts it on the air, and
knows nothing about *why* the key changes — rotating EC identities, arbitrary
data payloads, etc. are the caller's concern.

The 28-byte key is the link MTU. Anything that can produce 28-byte keys (a
rotating-key tag, a data encoder that packs bytes into keys, …) can drive this
component.

## Frame format

A Find My advertisement is 31 bytes (the legacy-advertising maximum) plus the
6-byte BLE random address. The 28-byte key is split across **both**:

| Field            | Bytes | Source                                  |
|------------------|-------|-----------------------------------------|
| BLE random addr  | 6     | `key[0..5]`, reversed, top 2 bits `0b11`|
| `0x1e 0xff 0x4c 0x00` | 4 | length + manufacturer-specific + Apple company id |
| `of_type` (`0x12`) | 1   | offline-finding type                    |
| `of_len` (`25`)  | 1     | length of remaining OF data             |
| `status`         | 1     | random                                  |
| `key_mid`        | 22    | `key[6..27]`                            |
| `key_hi`         | 1     | `key[0] >> 6` (recovers the 2 address bits clobbered by `0b11`) |
| `hint` (`0x00`)  | 1     |                                         |

Only the **28-byte key** traverses the Find My network — reports are indexed by
its hash. `of_type`/`of_len`/`status`/`hint` are framing a finder strips and
never forwards. A receiver reconstructs the key as:

```
key[0]     = (addr[5] & 0x3F) | (key_hi << 6)
key[1..5]  = addr[4..0]
key[6..27] = key_mid
```

The random address is forced to a *static random* address (top two bits `0b11`)
and rotates with the key. The component owns address rotation itself
(`CONFIG_BT_NIMBLE_HS_PVCY=n`); a static address would defeat the privacy design.

## API

```c
#include "findmy_adv.h"

esp_err_t findmy_adv_init(void (*on_ready)(void), uint32_t adv_interval_ms);
esp_err_t findmy_adv_set_key(const uint8_t key[FINDMY_ADV_KEY_LEN /* 28 */]);
```

- **`findmy_adv_init`** brings up the NimBLE port and spawns the host task. There
  is no shutdown path — `nimble_port_run` runs for the process lifetime.
  `on_ready` (optional, may be `NULL`) fires **once**, on the host task, when the
  stack first syncs; supply the initial key and arm any rotation timer there.
  `adv_interval_ms` pins `itvl_min == itvl_max` (one sweep of channels 37/38/39
  per window).
- **`findmy_adv_set_key`** buffers the key and, once synced, stops and restarts
  advertising under the new key/address. A key set before sync is broadcast on
  sync. On a controller reset + re-sync the buffered key is re-advertised
  automatically.

Both return `esp_err_t`: `ESP_OK`, `ESP_ERR_INVALID_ARG` (NULL key), the
`nimble_port_init` error, or `ESP_FAIL` on a NimBLE GAP failure.

### Threading

`findmy_adv_set_key` touches NimBLE GAP and holds **no lock** — it must be called
on the NimBLE host task. The `on_ready` callback already runs there; for periodic
updates, drive `set_key` from a `ble_npl_callout` on the default event queue
(`nimble_port_get_dflt_eventq()`), which also runs on the host task.

## Usage

```c
static void on_ready(void) {
    findmy_adv_set_key(current_key());   // publish the first key
    // ... arm a ble_npl_callout here to call findmy_adv_set_key() periodically
}

void app_main(void) {
    // ... bring up whatever produces keys ...
    ESP_ERROR_CHECK(findmy_adv_init(on_ready, 2000 /* ms */));
}
```

## Dependencies

`bt` (NimBLE host + controller) and `esp_hw_support` (`esp_random` for the status
byte). Requires ESP-IDF >= 6.0.
