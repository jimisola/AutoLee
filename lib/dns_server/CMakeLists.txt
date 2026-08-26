# Vendored verbatim from Espressif's official
# examples/protocols/http_server/captive_portal/components/dns_server
# (SPDX: Unlicense OR CC0-1.0). Answers every DNS A query so any device
# connecting to the AP is redirected into the captive portal.
idf_component_register(SRCS dns_server.c
                       INCLUDE_DIRS include
                       PRIV_REQUIRES esp_netif)
