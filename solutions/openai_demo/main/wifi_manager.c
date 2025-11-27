#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_http_server.h>
#include <esp_http_client.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "common.h" 

#define TAG "wifi_manager"

#define NVS_NAMESPACE         "wifi_creds"
#define NVS_KEY_SSID          "ssid"
#define NVS_KEY_PASS          "password"
#define NVS_AUTH_NAMESPACE    "auth"
#define NVS_AUTH_TOKEN_KEY    "auth_token"
#define MAX_AUTH_TOKEN_SIZE   512

#define DNS_PORT              53
#define DNS_MAX_LEN           256

static EventGroupHandle_t wifi_event_group = NULL;
static const int WIFI_CONNECTED_BIT = BIT0;
static bool g_wifi_connected = false;
static bool g_provisioning_mode = false;

static httpd_handle_t http_server = NULL;
static TaskHandle_t dns_task_handle = NULL;
static volatile bool dns_server_running = false;

static void (*g_on_creds_found)(const char *ssid, const char *pass) = NULL;

static bool wifi_credentials_exist(void);
static esp_err_t save_wifi_credentials(const char *ssid, const char *password);
static esp_err_t load_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len);
esp_err_t wifi_manager_clear_credentials(void);

static void ap_mode_task(void *param);
static void dns_server_task(void *pvParameters);
static void start_dns_server(void);
static void stop_dns_server(void);
static esp_err_t start_softap_provisioning(void);
static void start_http_server(void);
static void stop_http_server(void);

static esp_err_t captive_portal_handler(httpd_req_t *req);
static esp_err_t http_get_handler(httpd_req_t *req);
static esp_err_t http_configure_handler(httpd_req_t *req);
static void get_device_service_name(char *service_name, size_t max);

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data);

static char g_auth_token_buf[MAX_AUTH_TOKEN_SIZE] = {0};

const char *wifi_manager_get_auth_token(void)
{
    return g_auth_token_buf;
}

static esp_err_t auth_clear_token(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_AUTH_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS (auth): %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(nvs_handle, NVS_AUTH_TOKEN_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed erasing auth token: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Auth token cleared from NVS");
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    memset(g_auth_token_buf, 0, sizeof(g_auth_token_buf));  

    return err;
}

static bool wifi_credentials_exist(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return false;
    size_t len = 0;
    err = nvs_get_str(h, NVS_KEY_SSID, NULL, &len);
    nvs_close(h);
    return (err == ESP_OK && len > 0);
}

static esp_err_t save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) { nvs_close(h); return err; }
    err = nvs_set_str(h, NVS_KEY_PASS, password);
    if (err != ESP_OK) { nvs_close(h); return err; }
    err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) ESP_LOGI(TAG, "Saved WiFi credentials to NVS");
    return err;
}

static esp_err_t load_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) { nvs_close(h); return err; }
    err = nvs_get_str(h, NVS_KEY_PASS, password, &pass_len);
    nvs_close(h);
    return err;
}

esp_err_t wifi_manager_clear_credentials(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_erase_key(h, NVS_KEY_SSID);
    nvs_erase_key(h, NVS_KEY_PASS);
    nvs_commit(h);
    nvs_close(h);
    auth_clear_token();
    ESP_LOGI(TAG, "Cleared WiFi credentials from NVS");
    return ESP_OK;
}

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    static int s_retry_num = 0;

    ESP_LOGI(TAG, "Event: base=%s id=%ld",
             (event_base == WIFI_EVENT) ? "WIFI" : "IP", (long)event_id);

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started");
            led_controller_set_state(LED_STATE_CONNECTING);
            if (!g_provisioning_mode) {
                esp_wifi_connect();
            } else {
                ESP_LOGI(TAG, "Provisioning mode active - not connecting STA");
            }
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            if (g_provisioning_mode) {
                ESP_LOGI(TAG, "Ignoring STA disconnect due to provisioning mode");
                break;
            }
            led_controller_set_state(LED_STATE_CONNECTING);
            if (s_retry_num < 10) {
                s_retry_num++;
                ESP_LOGI(TAG, "Retrying connect (%d/10)...", s_retry_num);
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "Failed to connect after 10 attempts, entering AP mode");
                s_retry_num = 0;
                g_provisioning_mode = true;
                xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
                g_wifi_connected = false;
                led_controller_set_state(LED_STATE_ERROR);
                if (xTaskCreate(ap_mode_task, "ap_mode_task", 4096, NULL, 5, NULL) != pdPASS) {
                    ESP_LOGE(TAG, "Failed to create ap_mode_task");
                }
            }
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "Station connected to SoftAP");
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "Station disconnected from SoftAP");
            break;

        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        g_wifi_connected = true;
        g_provisioning_mode = false;

        led_controller_set_state(LED_STATE_CONNECTED);

        stop_dns_server();
        stop_http_server();

        wifi_config_t cfg;
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) {
            save_wifi_credentials((const char *)cfg.sta.ssid, (const char *)cfg.sta.password);
            if (g_on_creds_found) g_on_creds_found((const char *)cfg.sta.ssid, (const char *)cfg.sta.password);
        }

        auth_check_after_wifi();
    }
}

static void ap_mode_task(void *param)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "AP mode task: stopping wifi and starting AP+STA");

    stop_dns_server();
    stop_http_server();

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) ESP_LOGE(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(err));

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));

    err = esp_wifi_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));

    start_softap_provisioning();

    vTaskDelete(NULL);
}

static void dns_server_task(void *pvParameters)
{
    int sock = -1;
    struct sockaddr_in sa;
    struct sockaddr_in ra;

    ESP_LOGI(TAG, "DNS server task starting");

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket create failed");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(DNS_PORT);

    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        ESP_LOGE(TAG, "DNS socket bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    dns_server_running = true;
    ESP_LOGI(TAG, "DNS server bound to port %d", DNS_PORT);

    uint8_t rx[DNS_MAX_LEN];
    uint8_t tx[DNS_MAX_LEN];

    while (dns_server_running) {
        socklen_t ra_len = sizeof(ra);
        int len = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&ra, &ra_len);
        if (len > 0 && len >= 12) {
            memcpy(tx, rx, len);

            tx[2] = 0x81;
            tx[3] = 0x80;

            tx[6] = 0x00;
            tx[7] = 0x01;

            int tx_len = len;

            tx[tx_len++] = 0xC0;
            tx[tx_len++] = 0x0C;

            tx[tx_len++] = 0x00;
            tx[tx_len++] = 0x01;

            tx[tx_len++] = 0x00;
            tx[tx_len++] = 0x01;
            tx[tx_len++] = 0x00;
            tx[tx_len++] = 0x00;
            tx[tx_len++] = 0x00;
            tx[tx_len++] = 0x3C;

            tx[tx_len++] = 0x00;
            tx[tx_len++] = 0x04;
            /* 192.168.4.1 */
            tx[tx_len++] = 192;
            tx[tx_len++] = 168;
            tx[tx_len++] = 4;
            tx[tx_len++] = 1;

            sendto(sock, tx, tx_len, 0, (struct sockaddr *)&ra, ra_len);
        }
    }

    close(sock);
    dns_task_handle = NULL;
    ESP_LOGI(TAG, "DNS server stopped");
    vTaskDelete(NULL);
}

static void start_dns_server(void)
{
    if (dns_task_handle == NULL) {
        if (xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &dns_task_handle) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create DNS task");
            dns_task_handle = NULL;
        }
    }
}

static void stop_dns_server(void)
{
    if (dns_task_handle != NULL) {
        dns_server_running = false;
        vTaskDelay(pdMS_TO_TICKS(100));
        if (dns_task_handle != NULL) {
            vTaskDelete(dns_task_handle);
            dns_task_handle = NULL;
        }
    }
}

static const char *wifi_config_html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:Arial;text-align:center;padding:20px} .container{max-width:400px;margin:auto;background:#fff;padding:20px;border-radius:6px} input{width:100%;padding:8px;margin:8px 0} button{padding:10px 18px}</style>"
    "</head><body><div class='container'><h2>Noku ESP32 WiFi Setup</h2>"
    "<form action='/configure' method='post'>"
    "<input type='text' name='ssid' placeholder='SSID' required>"
    "<input type='password' name='password' placeholder='Password'>"
    "<button type='submit'>Connect</button></form></div></body></html>";

static esp_err_t captive_portal_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Captive portal request: %s", req->uri);

    if (strcmp(req->uri, "/generate_204") == 0 ||
        strcmp(req->uri, "/gen_204") == 0 ||
        strcmp(req->uri, "/ncsi.txt") == 0 ||
        strcmp(req->uri, "/connecttest.txt") == 0 ||
        strcmp(req->uri, "/hotspot-detect.html") == 0 ||
        strcmp(req->uri, "/library/test/success.html") == 0 ||
        strcmp(req->uri, "/success.txt") == 0) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, wifi_config_html, strlen(wifi_config_html));
    return ESP_OK;
}

static esp_err_t http_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, wifi_config_html, strlen(wifi_config_html));
    return ESP_OK;
}

static esp_err_t http_configure_handler(httpd_req_t *req)
{
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    char *buf = (char *)malloc(content_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, buf, content_len);
    if (ret <= 0) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Receive failed");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    char ssid_raw[256] = {0};
    char pass_raw[256] = {0};
    char ssid[33] = {0};
    char password[65] = {0};

    char *p = strstr(buf, "ssid=");
    if (p) {
        p += 5;
        char *amp = strchr(p, '&');
        if (amp) {
            int len = amp - p;
            if (len >= (int)sizeof(ssid_raw)) len = (int)sizeof(ssid_raw) - 1;
            strncpy(ssid_raw, p, len);
            ssid_raw[len] = '\0';
        } else {
            strncpy(ssid_raw, p, sizeof(ssid_raw) - 1);
        }
    }
    p = strstr(buf, "password=");
    if (p) {
        p += 9;
        int len = strlen(p);
        if (len >= (int)sizeof(pass_raw)) len = (int)sizeof(pass_raw) - 1;
        strncpy(pass_raw, p, len);
        pass_raw[len] = '\0';
    }

    free(buf);

    {
        int j = 0;
        for (int i = 0; ssid_raw[i] != '\0' && j < (int)sizeof(ssid)-1; ++i) {
            if (ssid_raw[i] == '+') { ssid[j++] = ' '; continue; }
            if (ssid_raw[i] == '%' &&
                isxdigit((unsigned char)ssid_raw[i+1]) &&
                isxdigit((unsigned char)ssid_raw[i+2])) {
                char hex[3] = { ssid_raw[i+1], ssid_raw[i+2], 0 };
                ssid[j++] = (char)strtol(hex, NULL, 16);
                i += 2;
                continue;
            }
            ssid[j++] = ssid_raw[i];
        }
        ssid[j] = '\0';
    }
    {
        int j = 0;
        for (int i = 0; pass_raw[i] != '\0' && j < (int)sizeof(password)-1; ++i) {
            if (pass_raw[i] == '+') { password[j++] = ' '; continue; }
            if (pass_raw[i] == '%' &&
                isxdigit((unsigned char)pass_raw[i+1]) &&
                isxdigit((unsigned char)pass_raw[i+2])) {
                char hex[3] = { pass_raw[i+1], pass_raw[i+2], 0 };
                password[j++] = (char)strtol(hex, NULL, 16);
                i += 2;
                continue;
            }
            password[j++] = pass_raw[i];
        }
        password[j] = '\0';
    }

    ESP_LOGI(TAG, "Received SSID: %s", ssid);

    wifi_config_t wifi_cfg = { 0 };
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
    } else {
        esp_err_t cerr = esp_wifi_connect();
        if (cerr != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_connect returned %s", esp_err_to_name(cerr));
        }
    }

    const char *response_fmt = "<!DOCTYPE html><html><body><h2>Configuration Received</h2><p>Connecting to: <b>%s</b></p><p>Device will restart...</p></body></html>";
    char resp[256];
    snprintf(resp, sizeof(resp), response_fmt, ssid);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, strlen(resp));

    save_wifi_credentials(ssid, password);
    g_provisioning_mode = false;

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

static void start_http_server(void)
{
    if (http_server != NULL) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;  
    config.lru_purge_enable = true;
    config.server_port = 80;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    if (httpd_start(&http_server, &config) == ESP_OK) {
        httpd_uri_t uri_root = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = http_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(http_server, &uri_root);

        httpd_uri_t uri_config = {
            .uri       = "/configure",
            .method    = HTTP_POST,
            .handler   = http_configure_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(http_server, &uri_config);

        const char *endpoints[] = {
            "/generate_204", "/gen_204", "/ncsi.txt", "/connecttest.txt",
            "/hotspot-detect.html", "/library/test/success.html", "/success.txt", "/*"
        };
        for (size_t i = 0; i < sizeof(endpoints)/sizeof(endpoints[0]); i++) {
            httpd_uri_t uri = {
                .uri = endpoints[i],
                .method = HTTP_GET,
                .handler = captive_portal_handler,
                .user_ctx = NULL
            };
            httpd_register_uri_handler(http_server, &uri);
        }

        ESP_LOGI(TAG, "HTTP server started");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        http_server = NULL;
    }
}

static void stop_http_server(void)
{
    if (http_server) {
        httpd_stop(http_server);
        http_server = NULL;
    }
}

static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t mac[6] = {0};
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err == ESP_OK) {
        snprintf(service_name, max, "NokuESP_%02X%02X", mac[4], mac[5]);
    } else {
        snprintf(service_name, max, "NokuESP_XXXX");
    }
}

static esp_err_t start_softap_provisioning(void)
{
    char service_name[32];
    get_device_service_name(service_name, sizeof(service_name));

    wifi_config_t ap_config = { 0 };
    strncpy((char *)ap_config.ap.ssid, service_name, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(service_name);
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 8; 

    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
        IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
        esp_netif_set_ip_info(ap_netif, &ip_info);
    }

    start_dns_server();
    start_http_server();

    ESP_LOGI(TAG, "SoftAP started: %s (open). Visit http://192.168.4.1", service_name);
    return ESP_OK;
}

void wifi_init(void)
{
    if (wifi_event_group == NULL) {
        wifi_event_group = xEventGroupCreate();
    }

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (wifi_credentials_exist()) {
        ESP_LOGI(TAG, "Stored WiFi credentials found, attempting STA connect");
        char ssid[33] = {0};
        char password[65] = {0};
        if (load_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password)) == ESP_OK) {
            wifi_config_t sta_cfg = { 0 };
            strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid)-1);
            strncpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password)-1);

            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
            ESP_ERROR_CHECK(esp_wifi_start());

            ESP_LOGI(TAG, "Attempting to connect to %s", ssid);
            return;
        }
    }

    ESP_LOGI(TAG, "No credentials or failed load - starting SoftAP provisioning");
    g_provisioning_mode = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());
    start_softap_provisioning();
}

static void auth_after_wifi_task(void *param)
{
    ESP_LOGI(TAG, "Auth: WiFi is connected. Checking token...");

    size_t size = sizeof(g_auth_token_buf);
    nvs_handle_t h;

    if (nvs_open(NVS_AUTH_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        esp_err_t err = nvs_get_str(h, NVS_AUTH_TOKEN_KEY, g_auth_token_buf, &size);
        nvs_close(h);

        if (err == ESP_OK && strlen(g_auth_token_buf) > 0) {
            ESP_LOGI(TAG, "Auth: Existing token loaded: %.20s...", g_auth_token_buf);
            vTaskDelete(NULL);
            return;
        }
    }

    ESP_LOGI(TAG, "Auth: No token in NVS. Generating new one...");

    ESP_LOGW(TAG, "Auth: Token generation skipped (feature disabled).");
    vTaskDelete(NULL);
    return;

    ESP_LOGI(TAG, "Auth: Token ready: %.20s...", g_auth_token_buf);
    vTaskDelete(NULL);
}

void auth_check_after_wifi(void)
{
    if (xTaskCreate(auth_after_wifi_task, "auth_after_wifi",
                    8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create auth_after_wifi_task");
    }
}

void wifi_manager_start(void (*on_creds_found)(const char *ssid, const char *pass))
{
    g_on_creds_found = on_creds_found;
    wifi_init();
}

bool wifi_manager_is_connected(void)
{
    return g_wifi_connected;
}
