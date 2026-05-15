idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
    REQUIRES micro_ecc mbedtls esp_hw_support nvs_flash bt
)

# Scrub all intermediate key material (mbedtls_platform_zeroize) on every path.
# Gated behind CONFIG_ESPTAG_ZEROIZE (menuconfig, default y); see CLAUDE.md
# "ZEROIZE build flag".
if(CONFIG_ESPTAG_ZEROIZE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ZEROIZE)
endif()

# Build an NVS image from the provisioned seed and flash it to the `nvs`
# partition as part of `idf.py flash`. Generate seed.csv once beforehand:
#   python3 scripts/gen_seed.py -o seed.csv
nvs_create_partition_image(nvs "${PROJECT_DIR}/seed.csv" FLASH_IN_PROJECT)
