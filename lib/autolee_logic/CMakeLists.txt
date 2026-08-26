# Header-only, hardware-independent logic. Shared by the firmware (main/
# EXTRA_COMPONENT_DIRS) and host_test/ (plain CMake host tests) - same
# headers, same tested code, two build systems.
idf_component_register(
    INCLUDE_DIRS "."
)
