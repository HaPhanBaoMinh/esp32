#include "network/network_service.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sys/time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs.h"

namespace apphub::network {
namespace {

constexpr char kTag[] = "network";
constexpr char kNamespace[] = "wifi";
constexpr char kSsidKey[] = "ssid";
constexpr char kPasswordKey[] = "pass";
// Không có IP sau khoảng này thì bật AP cấu hình (STA vẫn tiếp tục thử).
constexpr std::uint64_t kSetupApDelayUs = 20'000'000;
// Giữ AP thêm một lúc sau khi có IP để trang web cấu hình kịp nhận phản hồi.
constexpr std::uint64_t kSetupApLingerUs = 60'000'000;
constexpr std::uint32_t kMaxRetryDelayMs = 30'000;

struct Credentials {
    std::array<char, 33> ssid{};
    std::array<char, 65> password{};
};

std::mutex state_mutex;
Status current{};
Credentials credentials{};
bool started = false;
std::uint32_t retry_delay_ms = 1'000;
esp_netif_t* sta_netif{};
esp_netif_t* ap_netif{};
esp_timer_handle_t retry_timer{};
esp_timer_handle_t setup_ap_timer{};
esp_timer_handle_t ap_off_timer{};

template <std::size_t Size>
void copy_cstr(std::array<char, Size>& destination, const char* source) noexcept {
    destination.fill('\0');
    if (source != nullptr) {
        std::strncpy(destination.data(), source, Size - 1);
    }
}

bool has_credentials() noexcept {
    return credentials.ssid[0] != '\0';
}

esp_err_t load_credentials() noexcept {
    nvs_handle_t handle{};
    auto result = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (result != ESP_OK) {
        return result;
    }
    std::size_t size = credentials.ssid.size();
    if (nvs_get_str(handle, kSsidKey, credentials.ssid.data(), &size) != ESP_OK) {
        credentials.ssid.fill('\0');
    }
    size = credentials.password.size();
    if (nvs_get_str(handle, kPasswordKey, credentials.password.data(), &size) != ESP_OK) {
        credentials.password.fill('\0');
    }
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t apply_sta_config() noexcept {
    wifi_config_t config{};
    std::memcpy(config.sta.ssid, credentials.ssid.data(), sizeof(config.sta.ssid));
    std::memcpy(config.sta.password, credentials.password.data(),
                std::min(sizeof(config.sta.password), credentials.password.size()));
    config.sta.threshold.authmode =
        credentials.password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

void enable_setup_ap() noexcept {
    {
        std::lock_guard lock(state_mutex);
        if (current.ap_active) {
            return;
        }
    }
    esp_timer_stop(ap_off_timer);
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
        return;
    }
    std::lock_guard lock(state_mutex);
    current.ap_active = true;
    if (current.state != State::connected) {
        current.state = State::setup_ap;
    }
    ESP_LOGW(kTag, "Setup AP '%s' active at http://%s", current.ap_ssid.data(),
             current.ap_ip.data());
}

void disable_setup_ap() noexcept {
    {
        std::lock_guard lock(state_mutex);
        if (!current.ap_active || current.state != State::connected) {
            return;
        }
        current.ap_active = false;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(kTag, "Setup AP stopped");
}

void retry_timer_cb(void*) {
    if (has_credentials()) {
        esp_wifi_connect();
    }
}

void setup_ap_timer_cb(void*) {
    if (status().state != State::connected) {
        enable_setup_ap();
    }
}

void ap_off_timer_cb(void*) {
    disable_setup_ap();
}

void on_wifi_event(void*, esp_event_base_t base, std::int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (has_credentials()) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto* event = static_cast<wifi_event_sta_disconnected_t*>(data);
        {
            std::lock_guard lock(state_mutex);
            current.ip.fill('\0');
            current.rssi = 0;
            if (current.state == State::connected) {
                current.state = current.ap_active ? State::setup_ap : State::connecting;
            }
        }
        if (has_credentials()) {
            ESP_LOGW(kTag, "Disconnected (reason %d), retry in %u ms",
                     static_cast<int>(event->reason), static_cast<unsigned>(retry_delay_ms));
            esp_timer_stop(retry_timer);
            esp_timer_start_once(retry_timer, static_cast<std::uint64_t>(retry_delay_ms) * 1000);
            retry_delay_ms = std::min(retry_delay_ms * 2, kMaxRetryDelayMs);
            if (!esp_timer_is_active(setup_ap_timer)) {
                esp_timer_start_once(setup_ap_timer, kSetupApDelayUs);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto* event = static_cast<ip_event_got_ip_t*>(data);
        retry_delay_ms = 1'000;
        esp_timer_stop(setup_ap_timer);
        bool ap_active = false;
        {
            std::lock_guard lock(state_mutex);
            current.state = State::connected;
            std::snprintf(current.ip.data(), current.ip.size(), IPSTR,
                          IP2STR(&event->ip_info.ip));
            ap_active = current.ap_active;
            ESP_LOGI(kTag, "Connected to '%s', open http://%s or http://%s.local",
                     current.ssid.data(), current.ip.data(), kHostname);
        }
        if (ap_active) {
            esp_timer_stop(ap_off_timer);
            esp_timer_start_once(ap_off_timer, kSetupApLingerUs);
        }
    }
}

void on_time_sync(struct timeval*) {
    std::lock_guard lock(state_mutex);
    if (!current.time_synced) {
        ESP_LOGI(kTag, "Time synchronized via SNTP");
    }
    current.time_synced = true;
}

esp_err_t create_timer(esp_timer_cb_t callback, const char* name,
                       esp_timer_handle_t& handle) noexcept {
    esp_timer_create_args_t args{};
    args.callback = callback;
    args.name = name;
    return esp_timer_create(&args, &handle);
}

void start_mdns() noexcept {
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(kTag, "mDNS unavailable");
        return;
    }
    mdns_hostname_set(kHostname);
    mdns_instance_name_set("ESP32 App Hub");
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
}

}  // namespace

esp_err_t start() noexcept {
    if (started) {
        return ESP_OK;
    }

    auto result = load_credentials();
    if (result != ESP_OK) {
        return result;
    }
    if ((result = esp_netif_init()) != ESP_OK) {
        return result;
    }
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(sta_netif, kHostname);

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    if ((result = esp_wifi_init(&init_config)) != ESP_OK ||
        (result = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) {
        return result;
    }
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, nullptr);

    if ((result = create_timer(retry_timer_cb, "wifi_retry", retry_timer)) != ESP_OK ||
        (result = create_timer(setup_ap_timer_cb, "wifi_setup_ap", setup_ap_timer)) != ESP_OK ||
        (result = create_timer(ap_off_timer_cb, "wifi_ap_off", ap_off_timer)) != ESP_OK) {
        return result;
    }

    std::uint8_t mac[6]{};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap_config{};
    const int ssid_length = std::snprintf(reinterpret_cast<char*>(ap_config.ap.ssid),
                                          sizeof(ap_config.ap.ssid), "AppHub-%02X%02X",
                                          mac[4], mac[5]);
    ap_config.ap.ssid_len = static_cast<std::uint8_t>(ssid_length);
    std::strncpy(reinterpret_cast<char*>(ap_config.ap.password), kSetupApPassword,
                 sizeof(ap_config.ap.password) - 1);
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.max_connection = 4;
    ap_config.ap.channel = 1;

    esp_netif_ip_info_t ap_ip{};
    esp_netif_get_ip_info(ap_netif, &ap_ip);
    {
        std::lock_guard lock(state_mutex);
        copy_cstr(current.ap_ssid, reinterpret_cast<const char*>(ap_config.ap.ssid));
        std::snprintf(current.ap_ip.data(), current.ap_ip.size(), IPSTR, IP2STR(&ap_ip.ip));
        copy_cstr(current.ssid, credentials.ssid.data());
        current.state = has_credentials() ? State::connecting : State::setup_ap;
    }

    // Config AP chỉ đặt được khi AP interface đang bật, nên đặt ở APSTA rồi mới
    // về STA nếu đã có Wi-Fi. Không có Wi-Fi thì giữ APSTA để cấu hình qua web.
    if ((result = esp_wifi_set_mode(WIFI_MODE_APSTA)) != ESP_OK ||
        (result = esp_wifi_set_config(WIFI_IF_AP, &ap_config)) != ESP_OK ||
        (has_credentials() && (result = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK)) {
        return result;
    }
    if (has_credentials() && (result = apply_sta_config()) != ESP_OK) {
        return result;
    }
    if ((result = esp_wifi_start()) != ESP_OK) {
        return result;
    }
    if (has_credentials()) {
        esp_timer_start_once(setup_ap_timer, kSetupApDelayUs);
    } else {
        std::lock_guard lock(state_mutex);
        current.ap_active = true;
        ESP_LOGW(kTag, "No Wi-Fi saved. Join '%s' (password %s), open http://%s",
                 current.ap_ssid.data(), kSetupApPassword, current.ap_ip.data());
    }

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp_config.sync_cb = on_time_sync;
    esp_netif_sntp_init(&sntp_config);
    start_mdns();

    started = true;
    return ESP_OK;
}

Status status() noexcept {
    Status snapshot{};
    {
        std::lock_guard lock(state_mutex);
        snapshot = current;
    }
    if (snapshot.state == State::connected) {
        wifi_ap_record_t ap{};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            snapshot.rssi = ap.rssi;
        }
    }
    return snapshot;
}

const char* state_name(const State state) noexcept {
    switch (state) {
        case State::connecting:
            return "connecting";
        case State::connected:
            return "connected";
        case State::setup_ap:
            return "setup_ap";
        case State::stopped:
            break;
    }
    return "stopped";
}

void set_time_from_client(const std::int64_t unix_ms) noexcept {
    timeval value{};
    value.tv_sec = static_cast<time_t>(unix_ms / 1'000);
    value.tv_usec = static_cast<suseconds_t>((unix_ms % 1'000) * 1'000);
    settimeofday(&value, nullptr);
    std::lock_guard lock(state_mutex);
    if (!current.time_from_client) {
        ESP_LOGI(kTag, "Time set from web client");
    }
    current.time_from_client = true;
}

esp_err_t set_credentials(const std::string_view ssid, const std::string_view password) noexcept {
    if (ssid.empty() || ssid.size() >= credentials.ssid.size() ||
        password.size() >= credentials.password.size() ||
        (!password.empty() && password.size() < 8)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle{};
    auto result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    Credentials updated{};
    std::copy(ssid.begin(), ssid.end(), updated.ssid.begin());
    std::copy(password.begin(), password.end(), updated.password.begin());
    result = nvs_set_str(handle, kSsidKey, updated.ssid.data());
    if (result == ESP_OK) {
        result = nvs_set_str(handle, kPasswordKey, updated.password.data());
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    if (result != ESP_OK || !started) {
        return result;
    }

    credentials = updated;
    {
        std::lock_guard lock(state_mutex);
        copy_cstr(current.ssid, credentials.ssid.data());
        if (current.state != State::connected) {
            current.state = current.ap_active ? State::setup_ap : State::connecting;
        }
    }
    ESP_LOGI(kTag, "Saved Wi-Fi '%s', reconnecting", credentials.ssid.data());
    retry_delay_ms = 1'000;
    esp_wifi_disconnect();
    if ((result = apply_sta_config()) != ESP_OK) {
        return result;
    }
    esp_timer_stop(setup_ap_timer);
    esp_timer_start_once(setup_ap_timer, kSetupApDelayUs);
    // Sự kiện disconnect ở trên đã lên lịch kết nối lại; gọi thêm ở đây để không
    // phải chờ khi trước đó chưa từng kết nối.
    esp_wifi_connect();
    return ESP_OK;
}

}  // namespace apphub::network
