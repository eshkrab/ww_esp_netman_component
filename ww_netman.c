// ESP32 component for TCP/IP utilities over WiFi and Ethernet
// Rina Shkrabova, 2018
//
// Unless required by applicable law or agreed to in writing, this
// software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied.

#include "ww_netman.h"
#include "esp_log.h"

#define ESPNOW_CHANNEL 1
#define MAX_STA_CONN 5

#define TAG "TCP/IP:"
static const char *TAG_AP = "WiFi SoftAP";
static const char *TAG_STA = "WiFi Sta";

static int s_retry_num = 0;
#define EXAMPLE_ESP_MAXIMUM_RETRY 3

EventGroupHandle_t net_event_group;
// net_mode_t net_mode = MODE_NONE;

net_config_t net_config = {
  .mode = MODE_NONE,
  .dhcp= 0,
  .ip = {0},
  .subnet = {0},
  .gw = {0},
  .SSID = {0},
  .pswd = {0},
  .AP_SSID = {0},
  .AP_pswd = {0},
};

// ///////////////////////
// int dhcp_mode = 0;
// ///////////////////////
// char ip[16]={0};
// char subnet[16]={0};
// char gw[16]={0};
// ///////////////////////
// char SSID[30]={0};
// char pswd[30]={0};
// ///////////////////////
// char AP_SSID[30];
// char AP_pswd[30];

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {

  ///////////////////////
  // ETHERNET EVENTS
  ///////////////////////
  if (event_base == ETH_EVENT) {

    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
      case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
            mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
      case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
      case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
      case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
      default:
        break;
    }
  }
  ///////////////////////
  // WIFI EVENTS
  ///////////////////////
  else if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        ESP_LOGI(TAG_STA, "Station started");
        break;
      case WIFI_EVENT_STA_DISCONNECTED:
        {
          if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            xEventGroupClearBits(net_event_group, NET_CONNECTED_BIT);
            s_retry_num++;
            ESP_LOGW(TAG,"retry to connect to the AP");
          }
          ESP_LOGE(TAG,"connect to the AP failed\n");
          xEventGroupSetBits(net_event_group, STA_FAIL_BIT);
        }
        break;
      case WIFI_EVENT_AP_STACONNECTED:
        {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
        xEventGroupSetBits(net_event_group, STA_CONNECTED_BIT);
        }
        break;
      case WIFI_EVENT_AP_STADISCONNECTED:
        {
          wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
          ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
              MAC2STR(event->mac), event->aid);
          xEventGroupClearBits(net_event_group, STA_CONNECTED_BIT);
        }
        break;
    }
  }
  ///////////////////////
  // IP EVENTS
  ///////////////////////
  else if (event_base == IP_EVENT) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    switch (event_id) {
      case IP_EVENT_ETH_GOT_IP:
        // ESP_LOGI(TAG, "Ethernet Got IP Address");
        // ESP_LOGI(TAG, "~~~~~~~~~~~");
        // ESP_LOGI(TAG, "ETH IP:" IPSTR, IP2STR(&ip_info->ip));
        // ESP_LOGI(TAG, "ETH MASK:" IPSTR, IP2STR(&ip_info->netmask));
        // ESP_LOGI(TAG, "ETH GW:" IPSTR, IP2STR(&ip_info->gw));
        // ESP_LOGI(TAG, "~~~~~~~~~~~");
        xEventGroupSetBits(net_event_group, NET_CONNECTED_BIT);
        break;
      case IP_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "WiFi Got IP Address");
        ESP_LOGI(TAG, "~~~~~~~~~~~");
        ESP_LOGI(TAG, "WIFI IP:" IPSTR, IP2STR(&ip_info->ip));
        // ESP_LOGI(TAG, "WIFI MASK:" IPSTR, IP2STR(&ip_info->netmask));
        // ESP_LOGI(TAG, "WIFI GW:" IPSTR, IP2STR(&ip_info->gw));
        // ESP_LOGI(TAG, "~~~~~~~~~~~");

        s_retry_num = 0;
        xEventGroupSetBits(net_event_group, NET_CONNECTED_BIT);
        break;
    }
  }
}

esp_netif_t *wifi_init_sta(void)
{
  ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
  esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();
  esp_netif_ip_info_t ipInfo;

  if (!net_config.dhcp) {
    //have stop DHCP 
    esp_netif_dhcpc_stop(esp_netif_sta);

    //set the static IP
    ip4addr_aton(net_config.ip, &ipInfo.ip);
    ip4addr_aton(net_config.gw, &ipInfo.gw);
    ip4addr_aton(net_config.subnet, &ipInfo.netmask);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_sta, &ipInfo));
  }

  //set WIFI STA INFO
  wifi_config_t wifi_sta_config = {
    .sta = {
      .scan_method = WIFI_ALL_CHANNEL_SCAN,
      .failure_retry_cnt = EXAMPLE_ESP_MAXIMUM_RETRY,
      .threshold.authmode = {WIFI_AUTH_WPA2_PSK},
      .pmf_cfg = {
        .capable = {true},
        .required = {false},
      },
    },
  };
  strcpy((char *)wifi_sta_config.sta.ssid, (const char *)net_config.SSID);
  strcpy((char *)wifi_sta_config.sta.password, (const char *)net_config.pswd);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config) );

    ESP_LOGI(TAG_STA, "wifi_init_sta finished.");

    return esp_netif_sta;
}

esp_netif_t *wifi_init_softap(void)
{
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            // .ssid = AP_SSID,
            .ssid_len = strlen(net_config.AP_SSID),
            .channel = ESPNOW_CHANNEL,
            // .password = EXAMPLE_ESP_WIFI_AP_PASSWD,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
  strcpy((char *)wifi_ap_config.ap.ssid, (const char *)net_config.AP_SSID);
  strcpy((char *)wifi_ap_config.ap.password, (const char *)net_config.AP_pswd);



    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    ESP_LOGI(TAG_AP, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             net_config.AP_SSID, net_config.AP_pswd, ESPNOW_CHANNEL);

    return esp_netif_ap;
}

void initNetwork() {
  //print the mode
  ESP_LOGI(TAG, "Mode: %d", net_config.mode);
  ////////////////////////////////////
  //NON-VOLETILE STORAGE
  ////////////////////////////////////
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  //TCP/IP event handling & group (akin to flags and semaphores)
  net_event_group = xEventGroupCreate();

  ////////////////////////////////////
  //TCP/IP DRIVER INIT WITH A STATIC IP
  ////////////////////////////////////

  ESP_ERROR_CHECK(esp_netif_init());
  esp_netif_ip_info_t ipInfo;


  ////////////////////////////////////
  //EVENT HANDLER (CALLBACK)
  ////////////////////////////////////
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  if (net_config.mode == MODE_ETH) {
//     ////////////////////////////////////
//     //ETHERNET CONFIGURATION & INIT
//     ////////////////////////////////////
//     esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
//     esp_netif_t *netif = esp_netif_new(&cfg);
//
//
//      // Set default handlers to process TCP/IP stuffs
//     // ESP_ERROR_CHECK(esp_eth_set_default_handlers(netif));
//
//     ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
//     ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
//
//     if (!dhcp_mode){
//       //have stop DHCP
//       esp_netif_dhcpc_stop(netif);
//
//       //set the static IP
//       ip4addr_aton(ip, &ipInfo.ip);
//       ip4addr_aton(gw, &ipInfo.gw);
//       ip4addr_aton(subnet, &ipInfo.netmask);
//       ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ipInfo));
//     }
//
//     eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
//     eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
//
//     phy_config.phy_addr = CONFIG_PHY_ADDRESS;
//     phy_config.reset_gpio_num = CONFIG_PHY_POWER_PIN;
//     phy_config.reset_timeout_ms = 3000;
//
// #ifdef CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET
//     eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
//     esp32_emac_config.smi_mdc_gpio_num = CONFIG_EXAMPLE_ETH_MDC_GPIO;
//     esp32_emac_config.smi_mdio_gpio_num = CONFIG_EXAMPLE_ETH_MDIO_GPIO;
//     s_mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
// #else
//
//     gpio_install_isr_service(0);
//     spi_bus_config_t buscfg = {
//         .miso_io_num = CONFIG_EXAMPLE_ETH_SPI_MISO_GPIO,
//         .mosi_io_num = CONFIG_EXAMPLE_ETH_SPI_MOSI_GPIO,
//         .sclk_io_num = CONFIG_EXAMPLE_ETH_SPI_SCLK_GPIO,
//         .quadwp_io_num = -1,
//         .quadhd_io_num = -1,
//     };
//     ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_EXAMPLE_ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
//     spi_device_interface_config_t spi_devcfg = {
//         .mode = 0,
//         .clock_speed_hz = CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
//         .spics_io_num = CONFIG_EXAMPLE_ETH_SPI_CS_GPIO,
//         .queue_size = 20
//     };
// /* w5500 ethernet driver is based on spi driver */
//     eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(CONFIG_EXAMPLE_ETH_SPI_HOST, &spi_devcfg);
//     w5500_config.int_gpio_num = CONFIG_EXAMPLE_ETH_SPI_INT_GPIO;
//     s_mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
//     s_phy = esp_eth_phy_new_w5500(&phy_config);
// #endif
//
//     esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config);
//     esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);
//
//
//     esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
//
// //#ifdef CONFIG_PHY_CLOCK_GPIO0_IN
// //    config.clock_mode  = 0;// ETH_CLOCK_GPIO0_IN;
// //#endif
//
//     esp_eth_handle_t eth_handle = NULL;
//     ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));
//
//     /* attach Ethernet driver to TCP/IP stack */
//     ESP_ERROR_CHECK(esp_netif_attach(netif, esp_eth_new_netif_glue(eth_handle)));
//     /* start Ethernet driver state machine */
//     ESP_ERROR_CHECK(esp_eth_start(eth_handle));
//
//     ESP_ERROR_CHECK(esp_netif_get_ip_info(netif, &ipInfo));
//

  } else if (net_config.mode == MODE_WIFI) {
    ////////////////////////////////////
    //WIFI CONFIGURATION & INIT
    ////////////////////////////////////

    /* Register Event handler */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &event_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &event_handler,
                    NULL,
                    NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));


    //ESPNOW LONG RANGE, MAKE NO WIFI
    // ESP_ERROR_CHECK(esp_wifi_set_protocol( WIFI_IF_STA, WIFI_PROTOCOL_LR ));

    /* Initialize AP */
    ESP_LOGI(TAG_AP, "ESP_WIFI_MODE_AP");
    esp_netif_t *esp_netif_ap = wifi_init_softap();

    /* Initialize STA */
    ESP_LOGI(TAG_STA, "ESP_WIFI_MODE_STA");
    esp_netif_t *esp_netif_sta = wifi_init_sta();

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_ERROR_CHECK(esp_wifi_start() );


    EventBits_t bits = xEventGroupWaitBits(net_event_group,
        NET_CONNECTED_BIT | STA_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned,
     * hence we can test which event actually happened. */
    if (bits & NET_CONNECTED_BIT) {
      ESP_LOGI(TAG_STA, "connected to ap SSID:%s password:%s",
          net_config.SSID, net_config.pswd);
    } else if (bits & STA_FAIL_BIT) {
        ESP_LOGI(TAG_STA, "Failed to connect to SSID:%s, password:%s",
                 net_config.SSID, net_config.pswd);
    } else {
        ESP_LOGE(TAG_STA, "UNEXPECTED EVENT");
        return;
    }

    /* Set sta as the default interface */
    esp_netif_set_default_netif(esp_netif_sta);

    /* Enable napt on the AP netif */
    if (esp_netif_napt_enable(esp_netif_ap) != ESP_OK) {
        ESP_LOGE(TAG_STA, "NAPT not enabled on the netif: %p", esp_netif_ap);
    }


    // // ESP_ERROR_CHECK( esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) );
    // ESP_ERROR_CHECK(esp_netif_get_ip_info(netif, &ipInfo));
    //
    // vTaskDelay(500 / portTICK_PERIOD_MS);
    // ESP_LOGI(TAG, "wifi_init_sta finished.");
    // ESP_LOGI(TAG, "connect to ap SSID:%s password:%s", SSID, pswd);
    // ESP_LOGI(TAG, "IP:" IPSTR, IP2STR(&ipInfo.ip));
    //
  } else if (net_config.mode == MODE_STA_AP) {
    ////////////////////////////////////
    ///AP CONFIGURATION & INIT
    // initAP();

  }



  ////////////////////////////////////
  //TCP\IP INFORMATION PRINT
  ////////////////////////////////////
  ESP_LOGI(TAG, "TCP/IP init finished.");

}

char* generate_hostname(const char* prefix)
{
    uint8_t mac[6];
    char   *hostname;
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (-1 == asprintf(&hostname, "%s-%02X%02X%02X", prefix, mac[3], mac[4], mac[5])) {
        abort();
    }
    return hostname;
}

// start AP function
void initAP(){
  // esp_netif_t* wifiAP = esp_netif_create_default_wifi_ap();
  // esp_netif_ip_info_t ip_info;
  // esp_netif_ip_info_t ipInfo;
  //
  // ip4addr_aton(ip, &ipInfo.ip);
  // ip4addr_aton(gw, &ipInfo.gw);
  // ip4addr_aton(subnet, &ipInfo.netmask);
  //
  // esp_netif_dhcps_stop(wifiAP);
  // esp_netif_set_ip_info(wifiAP, &ipInfo);
  // esp_netif_dhcps_start(wifiAP);
  //
  //
  // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  // ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  //
  //
  // wifi_config_t wifi_config = {
  //     .ap = {
  //         .channel = CONFIG_AP_CHANNEL,
  //         .authmode = CONFIG_AP_AUTHMODE,
  //         .ssid_hidden = CONFIG_AP_SSID_HIDDEN,
  //         .max_connection = CONFIG_AP_MAX_CONNECTIONS,
  //         .beacon_interval = CONFIG_AP_BEACON_INTERVAL,
  //     },
  // };
  //
  // const char *AP_SSID = generate_hostname((const char *)SSID);
  // strcpy((char *)wifi_config.ap.ssid, (const char *)AP_SSID);
  // strcpy((char *)wifi_config.ap.password, (const char *)pswd);
  //
  // if (strlen(pswd) == 0) {
  //     wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  // }
  //
  // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  // ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  // ESP_ERROR_CHECK(esp_wifi_start());
  //
  // ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
  //          SSID, pswd, CONFIG_AP_CHANNEL);
  //
  //
  // ESP_LOGI("AccessPoint", "- adapter starting...\n");
  //
}
