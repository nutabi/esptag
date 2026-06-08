#ifndef ESPTAG_COMMON
#define ESPTAG_COMMON

/*
 * Project-wide vocabulary shared across modules (crypto, tag, nvs_store,
 * ble_adv). Holds only what more than one module needs, so that tag and
 * nvs_store can depend on this instead of the crypto module header.
 */

/*
 * Error-handling convention
 * -------------------------
 * Every public function in this firmware returns a plain int: 0 on success,
 * nonzero (1) on any failure. This is a deliberate house convention, not an
 * esp_err_t passthrough. No error *code* is propagated to the caller; the
 * failing function logs the specific esp_err_t / PSA / NimBLE status at the
 * failure point (ESP_LOGE) and collapses it to 1, and callers treat nonzero as
 * fatal. Keep new code to this 0/1 convention rather than returning esp_err_t.
 * Programmer-error preconditions that cannot vary at runtime use assert()
 * instead (see the kdf checks in crypto.c).
 *
 * One exception, deliberately inverted: crypto.c's uecc_rng must follow
 * micro-ecc's RNG contract (1 = success) — it is commented at the call site.
 */

/* Shared size constants (the byte-buffer vocabulary, all big-endian).
 * Crypto-internal sizes (HASH_LEN, N_LEN) stay in crypto.h. */
#define SK_LEN 32  // symmetric ratchet key (sk_0 / sk_curr)
#define D_LEN  28  // private scalar (d_0 / d_i)
#define P_LEN  28  // compressed advertising key broadcast each epoch (p_curr)

#endif // ESPTAG_COMMON
