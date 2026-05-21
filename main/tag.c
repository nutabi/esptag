#include "tag.h"

#include "crypto.h"

#include <string.h>

#include "esp_log.h"

#include "mbedtls/platform_util.h"

#define LOG_TAG "tag"

/* Header implementation */

int tag_init(tag_t *tag) {
    if (tag == NULL) {
        ESP_LOGE(LOG_TAG, "tag is null");
        return 1;
    }

    // counter is not persisted; it always starts at 0 on boot.
    tag->counter = 0;
    memcpy(tag->sk_curr, tag->sk_0, SK_LEN);

    // Derive the epoch-0 advertising key: p_0 = derive_p(d_0, sk_0).
    if (crypto_derive_p(tag->d_0, tag->sk_curr, tag->p_curr) != 0) {
        ESP_LOGE(LOG_TAG, "p derivation failed");
        return 1;
    }
    return 0;
}

int tag_rotate(tag_t *tag) {
    if (tag == NULL) {
        ESP_LOGE(LOG_TAG, "tag is null");
        return 1;
    }

    // Update SK
    uint8_t sk_next[SK_LEN];
    if (crypto_update_sk(tag->sk_curr, sk_next) != 0) {
        ESP_LOGE(LOG_TAG, "sk rotation failed");
#ifdef ZEROIZE
        mbedtls_platform_zeroize(sk_next, sizeof(sk_next));
#endif // ZEROIZE
        return 1;
    }

    // Derive P
    if (crypto_derive_p(tag->d_0, sk_next, tag->p_curr) != 0) {
        ESP_LOGE(LOG_TAG, "p derivation failed");
#ifdef ZEROIZE
        mbedtls_platform_zeroize(sk_next, sizeof(sk_next));
#endif // ZEROIZE
        return 1;
    }

    // Update tag
    memcpy(tag->sk_curr, sk_next, SK_LEN);
    tag->counter++;
#ifdef ZEROIZE
    mbedtls_platform_zeroize(sk_next, sizeof(sk_next));
#endif // ZEROIZE
    return 0;
}

int tag_destroy(tag_t *tag) {
    if (tag == NULL) {
        ESP_LOGE(LOG_TAG, "tag is null");
        return 1;
    }

#ifdef ZEROIZE
    mbedtls_platform_zeroize(tag, sizeof(*tag));
#endif // ZEROIZE
    return 0;
}
