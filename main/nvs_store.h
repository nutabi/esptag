#ifndef ESPTAG_NVS_STORE
#define ESPTAG_NVS_STORE

#include <stdint.h>

#include "tag.h"

/**
 * @brief Initialise the default NVS flash partition.
 *
 * Call once at startup before any load/save. Any failure (including a corrupt
 * partition) is fatal; recovery means re-flashing.
 *
 * @return 0 on success, nonzero on any failure.
 */
int nvs_store_init(void);

/**
 * @brief Load the provisioned seed (d_0, sk_0) from NVS into the tag.
 *
 * The seed is written to the NVS partition at flash time from seed.csv; the
 * firmware never writes it back. Only d_0 and sk_0 are populated; call
 * tag_init() to derive the rest of the runtime state.
 *
 * @param tag Tag whose d_0 and sk_0 are filled in.
 * @return 0 on success; nonzero if the namespace or either key is absent (device
 *         not provisioned), in which case the caller should not continue.
 */
int nvs_store_load_tag(tag_t *tag);

/**
 * @brief Load the persisted rotation counter from the writable state namespace.
 *
 * A missing namespace/key reads back as 0 (first boot since provisioning), not
 * an error. Used at boot to fast-forward the ratchet (crypto_advance_sk) so
 * identifiers do not replay across reboots.
 *
 * @param counter Out: the persisted counter, or 0 if none is stored yet.
 * @return 0 on success (including the absent case), nonzero only on an
 *         unexpected NVS error.
 */
int nvs_store_load_counter(uint32_t *counter);

/**
 * @brief Persist the rotation counter to the writable state namespace.
 *
 * Opens the namespace, sets the key, and commits. Called after each epoch
 * advance when counter persistence is enabled.
 *
 * @param counter The counter value to store.
 * @return 0 on success, nonzero on any NVS failure.
 */
int nvs_store_save_counter(uint32_t counter);

#endif // ESPTAG_NVS_STORE
