# Pull the single unit-under-test source (crypto.c) in by path -- `main` is the
# project's app component and isn't requestable, and we don't want main.c /
# ble_adv / nvs / tag dragged in. INCLUDE_DIRS ../../main exposes crypto.h.
idf_component_register(
    SRCS "test_crypto.c" "../../main/crypto.c"
    INCLUDE_DIRS "." "../../main"
    REQUIRES unity micro_ecc mbedtls esp_hw_support
)

# crypto.c's secret-scrubbing is gated behind #ifdef CONFIG_ESPTAG_ZEROIZE. In
# the firmware that symbol comes from main's Kconfig (menuconfig), but this test
# project pulls crypto.c in by path and doesn't see main's Kconfig -- so we
# define it on the command line here, pinned on unconditionally. The tests
# should always exercise the scrubbing path regardless of how the firmware is
# configured.
target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_ESPTAG_ZEROIZE=1)
