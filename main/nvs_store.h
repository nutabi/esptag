#ifndef ESPTAG_NVS_STORE
#define ESPTAG_NVS_STORE

#include <stdint.h>

#include "tag.h"

/**
 * Initialise the default NVS flash partition.
 *
 * Must be called once at startup before loading. Returns nonzero on any
 * failure (including a corrupt partition); recovery means re-flashing.
 */
int nvs_store_init(void);

/**
 * Load the provisioned tag seed (d_0, sk_0) from NVS into the tag.
 *
 * The seed is written to the NVS partition at flash time from seed.csv; the
 * firmware never writes it back. Returns nonzero if the namespace or either
 * key is absent (device not provisioned), in which case the caller should not
 * continue. Only d_0 and sk_0 are populated; call tag_init() to derive the
 * rest of the runtime state.
 */
int nvs_store_load_tag(tag_t *tag);

/**
 * Load the persisted rotation counter from the writable state namespace.
 *
 * Writes the stored value into *counter, or 0 if the namespace/key is absent
 * (first boot since provisioning). Returns nonzero only on an unexpected NVS
 * error. Used at boot to fast-forward the ratchet so identifiers do not replay
 * across reboots; see crypto_advance_sk.
 */
int nvs_store_load_counter(uint32_t *counter);

/**
 * Persist the rotation counter to the writable state namespace (open, set,
 * commit). Returns nonzero on any NVS failure. Called after each epoch advance
 * when counter persistence is enabled.
 */
int nvs_store_save_counter(uint32_t counter);

#endif // ESPTAG_NVS_STORE
