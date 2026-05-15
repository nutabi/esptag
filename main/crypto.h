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

int crypto_init(void);

int crypto_advance_sk(const uint8_t sk_0[SK_LEN],
                      uint32_t counter,
                      uint8_t sk_i[SK_LEN]);

int crypto_update_sk(const uint8_t sk_prev[SK_LEN],
                     uint8_t sk_next[SK_LEN]);

int crypto_derive_p(const uint8_t d_0[D_LEN],
                    const uint8_t sk_i[SK_LEN],
                    uint8_t p_i[P_LEN]);

#endif // ESPTAG_CRYPTO
