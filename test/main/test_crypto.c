/* Known-answer tests (KAT) for the esptag crypto core.
 *
 * Vectors in kat_vectors.h are produced by scripts/gen_kat.py, an INDEPENDENT
 * reference implementation (Python `cryptography` for the EC math), so a bug in
 * crypto.c cannot leak into the expected values.
 *
 * Coverage via the public API only (kdf/hash/compute_d/d_to_p are static):
 *   - crypto_update_sk  -> single-block KDF
 *   - crypto_advance_sk -> ratchet loop + buffer swap (counter=2)
 *   - crypto_derive_p   -> 3-block KDF (incl. final 8-byte truncation),
 *                          d_i = d_0*u + v mod n, and point x-coordinate
 *
 * Driven directly from app_main with UNITY_BEGIN/RUN_TEST/UNITY_END (no
 * interactive Unity menu), so it runs the same on the linux host target and on
 * esp32s3 captured over serial.
 */

#include "unity.h"

#include "crypto.h"
#include "kat_vectors.h"

void setUp(void) {}
void tearDown(void) {}

static void test_update_sk_matches_kat(void)
{
    uint8_t sk_next[SK_LEN];
    TEST_ASSERT_EQUAL_INT(STATUS_OK, crypto_update_sk(KAT_SK_PREV, sk_next));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(KAT_SK_NEXT, sk_next, SK_LEN);
}

static void test_advance_sk_counter2_matches_kat(void)
{
    uint8_t sk_i[SK_LEN];
    TEST_ASSERT_EQUAL_INT(STATUS_OK, crypto_advance_sk(KAT_SK_0, 2, sk_i));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(KAT_SK_2, sk_i, SK_LEN);
}

static void test_advance_sk_counter0_is_identity(void)
{
    uint8_t sk_i[SK_LEN];
    TEST_ASSERT_EQUAL_INT(STATUS_OK, crypto_advance_sk(KAT_SK_0, 0, sk_i));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(KAT_SK_0, sk_i, SK_LEN);
}

static void test_advance_sk_counter1_equals_update_sk(void)
{
    uint8_t via_advance[SK_LEN];
    uint8_t via_update[SK_LEN];
    TEST_ASSERT_EQUAL_INT(STATUS_OK, crypto_advance_sk(KAT_SK_PREV, 1, via_advance));
    TEST_ASSERT_EQUAL_INT(STATUS_OK, crypto_update_sk(KAT_SK_PREV, via_update));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(via_update, via_advance, SK_LEN);
}

static void test_derive_p_matches_kat(void)
{
    uint8_t p_i[P_LEN];
    TEST_ASSERT_EQUAL_INT(STATUS_OK, crypto_derive_p(KAT_D_0, KAT_SK_I, p_i));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(KAT_P_I, p_i, P_LEN);
}

void app_main(void)
{
    TEST_ASSERT_EQUAL_INT(STATUS_OK, crypto_init());

    UNITY_BEGIN();
    RUN_TEST(test_update_sk_matches_kat);
    RUN_TEST(test_advance_sk_counter2_matches_kat);
    RUN_TEST(test_advance_sk_counter0_is_identity);
    RUN_TEST(test_advance_sk_counter1_equals_update_sk);
    RUN_TEST(test_derive_p_matches_kat);
    UNITY_END();
}
