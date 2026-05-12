idf_component_register(
        SRCS "uECC.c"
        INCLUDE_DIRS "."
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
        uECC_SUPPORTS_secp224r1=1
        uECC_SUPPORT_COMPRESSED_POINT=1
        uECC_OPTIMIZATION_LEVEL=2
)