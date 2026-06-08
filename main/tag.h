#ifndef ESPTAG_TAG
#define ESPTAG_TAG

#include <stdint.h>

#include "crypto.h"

// Secret-bearing struct
typedef struct {
    uint8_t         d_0[D_LEN];         // Initial private scalar
    uint8_t         sk_0[SK_LEN];       // Initial symmetric key

    uint8_t         sk_curr[SK_LEN];    // Current symmetric key
    uint32_t        counter;            // SK counter
    uint8_t         p_curr[P_LEN];      // Current advertising key
} tag_t;

/**
 * Initialise runtime state from a provisioned seed, resuming at `counter`.
 *
 * Assumes d_0 and sk_0 are already populated (e.g. by nvs_store_load_tag).
 * Sets tag->counter to `counter`, fast-forwards sk_curr from sk_0 by that many
 * ratchet steps (crypto_advance_sk), and derives the matching advertising key
 * p_curr. Pass the persisted counter (nvs_store_load_counter) so identifiers
 * resume rather than replay across reboots; counter == 0 is the first-boot /
 * fresh-seed case (sk_curr == sk_0). Must be called once before tag_rotate.
 */
int tag_init(tag_t *tag, uint32_t counter);

/**
 * Rotate tag to the next set of keys
 */
int tag_rotate(tag_t *tag);

/**
 * Zeroize data in tag
 */
int tag_destroy(tag_t *tag);

#endif // ESPTAG_TAG
