idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS include                       # Edit following two lines to set component requirements (see docs)
                       REQUIRES nvs_flash esp_eth esp_netif esp_event esp_timer esp_wifi esp_http_client
                       PRIV_REQUIRES )

