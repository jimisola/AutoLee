# Vendored from hoeken/PsychicHttp (MIT), src/ + LICENSE only (examples/ and
# its Arduino/ArduinoJson/esp_littlefs submodules are not needed - we're
# always the pure-ESP-IDF path, no Arduino component in this project).
file(GLOB PSYCHIC_HTTP_SRCS "src/*.cpp")

idf_component_register(
    SRCS ${PSYCHIC_HTTP_SRCS}
    INCLUDE_DIRS "src"
    REQUIRES esp_https_server esp_http_server esp_netif mbedtls arduinojson
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC -DESP32)
target_compile_options(${COMPONENT_LIB} PRIVATE -fno-rtti)
