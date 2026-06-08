#ifndef ESPTAG_CRYPTO
#define ESPTAG_CRYPTO

#include <stdint.h>

/* Size constants */

#define SK_LEN 32
#define P_LEN 28
#define D_LEN 28
#define HASH_LEN 32
#define N_LEN 28

/* P-224 (secp224r1) group order n, big-endian */

extern const uint8_t P224_N[N_LEN];

/* Functions */
/* All byte arrays are big-endian. */

/**
 * @brief Initialise the crypto core: bring up PSA and select the secp224r1
 *        curve.
 *
 * Call once at startup before any other crypto_* function.
 *
 * @return 0 on success, nonzero on failure.
 */
int crypto_init(void);

/**
 * @brief Fast-forward the symmetric-key ratchet: sk_i = update^counter(sk_0).
 *
 * Applies crypto_update_sk `counter` times starting from sk_0. counter == 0
 * just copies sk_0 unchanged. Used to recompute the ratchet state for a given
 * epoch from the provisioned seed (e.g. after a reboot).
 *
 * @param sk_0    Seed symmetric key to ratchet from.
 * @param counter Number of ratchet steps to apply (the target epoch).
 * @param sk_i    Out: the ratchet key after `counter` steps.
 * @return 0 on success, nonzero on failure.
 */
int crypto_advance_sk(const uint8_t sk_0[SK_LEN],
                      uint32_t counter,
                      uint8_t sk_i[SK_LEN]);

/**
 * @brief One ratchet step: sk_next = KDF(sk_prev, "update", 32).
 *
 * @param sk_prev Current ratchet key.
 * @param sk_next Out: the next ratchet key.
 * @return 0 on success, nonzero on failure.
 */
int crypto_update_sk(const uint8_t sk_prev[SK_LEN],
                     uint8_t sk_next[SK_LEN]);

/**
 * @brief Derive the advertising key for one epoch from the seed scalar d_0 and
 *        the current ratchet key sk_i.
 *
 *   (u || v) = KDF(sk_i, "diversify", 72)
 *   d_i      = d_0 * u + v mod n
 *   p_i      = compress(d_i * G), with the 1-byte point header stripped
 *
 * p_i is the 28-byte compressed x-coordinate that gets broadcast.
 *
 * @param d_0  Seed private scalar.
 * @param sk_i Ratchet key for the epoch being derived.
 * @param p_i  Out: the 28-byte compressed advertising key.
 * @return 0 on success, nonzero on failure.
 */
int crypto_derive_p(const uint8_t d_0[D_LEN],
                    const uint8_t sk_i[SK_LEN],
                    uint8_t p_i[P_LEN]);

#endif // ESPTAG_CRYPTO
