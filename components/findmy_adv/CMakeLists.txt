# Reusable "link layer" for Apple offline-finding (Find My-style) broadcasting:
# packages a 28-byte advertising key into a valid Find My advertisement and puts
# it on the air. Knows nothing about the tag/rotation/EC layers above it.
#   bt             — NimBLE host + controller
#   esp_hw_support — esp_random (status byte)
# Returns esp_err_t (the platform convention for a standalone component), so it
# carries no dependency on the firmware's internal status_t / common.h.
idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
    REQUIRES bt esp_hw_support
)
