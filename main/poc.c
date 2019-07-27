/*  POC Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <esp_http_server.h>
	
#include "lwip/err.h"
#include "lwip/sys.h"

/* The examples use WiFi configuration that you can set via 'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_MAX_STA_CONN       CONFIG_MAX_STA_CONN

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

static const char *TAG = "POC";

#define MAX_APS 20

// From auth_mode code to string
static char* getAuthModeName(wifi_auth_mode_t auth_mode) {

	char *names[] = { "OPEN", "WEP", "WPA PSK", "WPA2 PSK", "WPA WPA2 PSK", "MAX" };

	return names[auth_mode];
}

#define RESP_FORM "<form action=\"/\" method=\"post\">" \
        "<label for=\"ssid\">SSID: </label>" \
        "<input name=\"ssid\" id=\"ssid\" value=\"\"><br>" \
        "<label for=\"pass\">Password: </label>" \
        "<input name=\"pass\" id=\"pass\" value=\"\"><br>" \
        "<button>Send</button></body></html>"

esp_err_t uri_get_handler(httpd_req_t *req)
{
    char buf[200];
    uint16_t ap_num = MAX_APS;

    wifi_scan_config_t wifi_scan_config = {
            .ssid = 0,
            .bssid = 0, 
            .channel = 0,
	    .show_hidden = true
    };
    ESP_ERROR_CHECK(esp_wifi_scan_start(&wifi_scan_config, true));
    wifi_ap_record_t ap_records[MAX_APS];
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, ap_records)); 

    if (ap_num >= MAX_APS) {
	return ESP_FAIL;
    }
    
    snprintf(buf, sizeof(buf), "<html><body>Found %d access points:<br><br>", ap_num);
    httpd_resp_send_chunk(req, buf, strlen(buf));
    snprintf(buf, sizeof(buf), "<table><tr><th>SSID</th><th>Channel</th><th>RSSI</th><th>Auth Mode</th></tr>");
    httpd_resp_send_chunk(req, buf, strlen(buf));

    for(int i = 0; i < ap_num; i++) {
	snprintf(buf, sizeof(buf), "<tr><td>%s</td><td>%d</td><td>%d</td><td>%s</td></tr>", 
			(char *)ap_records[i].ssid, ap_records[i].primary, ap_records[i].rssi,
			getAuthModeName(ap_records[i].authmode));
	httpd_resp_send_chunk(req, buf, strlen(buf));
    }

    snprintf(buf, sizeof(buf), "</table><br><br>");
    httpd_resp_send_chunk(req, buf, strlen(buf));

    /* Respond with the accumulated value */
    httpd_resp_send_chunk(req, RESP_FORM, strlen(RESP_FORM));
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

int wifi_connect_sta(char *ssid, char *pass)
{
    wifi_config_t wifi_config;
   
    memset(&wifi_config, 0x0, sizeof(wifi_config_t));

    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.bssid_set = 0;
    wifi_config.sta.listen_interval = 1; // Without increased interval doesn't connect ???

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    
    // Restart Wifi (The Mode APSTA should work)
    ESP_ERROR_CHECK(esp_wifi_start());
    
    /* Connect to AP */
    if (esp_wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect WiFi");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connecting to AP [%s] ...", ssid);
    return 0;
}

httpd_uri_t uri_get = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = uri_get_handler,
};

esp_err_t uri_post_handler(httpd_req_t *req)
{
    char buf[100];
    char *ssid,*pass;
    int ret;

    memset(buf, 0x0, sizeof(buf));

    // Read client's socket
    ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
	if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
		httpd_resp_send_408(req);
		return ESP_FAIL;
	}
    }

    ESP_LOGI(TAG,"Received data from user phone: %s", buf);

    ssid = strstr(buf, "ssid=");
    if (!ssid) {
	httpd_resp_send(req, RESP_FORM, strlen(RESP_FORM));
	return ESP_FAIL;
    }

    ssid = ssid + strlen("ssid=");
    for (pass = ssid; *pass != '\0' && *pass != '&'; pass++)
	    if (*pass == '+')
		    *pass = ' ';

    if (*pass != '&') {
	httpd_resp_send(req, RESP_FORM, strlen(RESP_FORM));
	return ESP_FAIL;
    }

    *pass = '\0'; pass++;
    if (strncmp(pass, "pass=", strlen("pass="))) {
	httpd_resp_send(req, RESP_FORM, strlen(RESP_FORM));
	return ESP_FAIL;
    }

    pass = pass + strlen("pass=");
    if (!*pass) {
	httpd_resp_send(req, RESP_FORM, strlen(RESP_FORM));
	return ESP_FAIL;
    }

    if (wifi_connect_sta(ssid, pass) < 0) {
	return ESP_FAIL;
    }

    snprintf(buf, sizeof(buf),"OK. Connecting to ssid: %s ...", ssid);
    httpd_resp_send(req, buf, strlen(buf));

    return ESP_OK;
}

httpd_uri_t uri_post = {
    .uri      = "/",
    .method   = HTTP_POST,
    .handler  = uri_post_handler,
};

httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on address: port: '%d'", config.server_port);
    httpd_handle_t server;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_post);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}


static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    httpd_handle_t *server = (httpd_handle_t *) ctx;

    switch(event->event_id) {
    case SYSTEM_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "station:"MACSTR" join, AID=%d",
                 MAC2STR(event->event_info.sta_connected.mac),
                 event->event_info.sta_connected.aid);
	
	// If user phone is connected start webserver
        if (*server == NULL) {
		*server = start_webserver();
	}
	
	break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        ESP_LOGI(TAG, "station:"MACSTR"leave, AID=%d",
                 MAC2STR(event->event_info.sta_disconnected.mac),
                 event->event_info.sta_disconnected.aid);
       
        // If the user phone is disconnected stop webserver	
	if (*server) {
		stop_webserver(*server);
		*server = NULL;
	}
	break;
    case SYSTEM_EVENT_STA_START:
	ESP_LOGI(TAG,"STA_START event\n");
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
	ESP_LOGI(TAG,"connect to the AP fail:: station %32s reason %d\n", (char*)event->event_info.disconnected.ssid,
	       event->event_info.disconnected.reason	);
	
	/* Set code corresponding to the reason for disconnection */
        switch (event->event_info.disconnected.reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_BEACON_TIMEOUT:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            ESP_LOGI(TAG, "STA Auth Error");
            break;
        case WIFI_REASON_NO_AP_FOUND:
            ESP_LOGI(TAG, "STA AP Not found");
	    esp_wifi_disconnect();
            break;
        default:
            ESP_LOGI(TAG, "STA connect again");
            esp_wifi_connect();
        }
	break;
    case SYSTEM_EVENT_STA_GOT_IP:
	ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        break;	
    default:
        break;
    }
    return ESP_OK;
}

void wifi_init_ap_sta(httpd_handle_t *server)
{
    s_wifi_event_group = xEventGroupCreate();

    // Initialize TCP/IP stack
    tcpip_adapter_init();
    
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, server));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    
    // Initialize WiFi
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Config for SoftAP mode
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    
    // if there is no password make AP open
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // Initialize WiFi in SoftAP and STA mode simultaniously
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    
    // Start SoftAP
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_ap_sta finished. ssid:%s password:%s",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

void app_main()
{
    static httpd_handle_t server = NULL;

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP_STA");
    // Initialize WiFi interface 
    wifi_init_ap_sta(&server);
}