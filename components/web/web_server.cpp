#include "web/web_server.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "network/network_service.hpp"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

namespace apphub::web {
namespace {

constexpr char kTag[] = "web";
constexpr std::size_t kMaxJsonBody = 4096;
constexpr std::size_t kOtaChunkSize = 4096;
constexpr std::uint64_t kRestartDelayUs = 1'500'000;

const AppRegistry* app_registry{};
httpd_handle_t server{};
esp_timer_handle_t restart_timer{};

std::mutex settings_mutex;
Settings published{};
bool has_published = false;
Settings pending{};
bool has_pending = false;

struct JsonDeleter {
    void operator()(cJSON* json) const noexcept { cJSON_Delete(json); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

struct FreeDeleter {
    void operator()(char* text) const noexcept { cJSON_free(text); }
};

esp_err_t send_json(httpd_req_t* request, const cJSON* json) {
    std::unique_ptr<char, FreeDeleter> body(cJSON_PrintUnformatted(json));
    if (!body) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, body.get());
}

esp_err_t send_ok(httpd_req_t* request) {
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

esp_err_t send_error(httpd_req_t* request, const httpd_err_code_t code, const char* message) {
    return httpd_resp_send_err(request, code, message);
}

void schedule_restart() {
    esp_timer_stop(restart_timer);
    esp_timer_start_once(restart_timer, kRestartDelayUs);
}

// Đọc toàn bộ body JSON (giới hạn kích thước) và parse.
JsonPtr read_json_body(httpd_req_t* request) {
    if (request->content_len == 0 || request->content_len > kMaxJsonBody) {
        return nullptr;
    }
    std::unique_ptr<char[]> buffer(new (std::nothrow) char[request->content_len + 1]);
    if (!buffer) {
        return nullptr;
    }
    std::size_t received = 0;
    while (received < request->content_len) {
        const int count =
            httpd_req_recv(request, buffer.get() + received, request->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (count <= 0) {
            return nullptr;
        }
        received += static_cast<std::size_t>(count);
    }
    buffer[received] = '\0';
    return JsonPtr(cJSON_Parse(buffer.get()));
}

bool app_enabled(const Settings& settings, const AppMetadata& metadata) {
    const auto* preference = find_app_preference(settings, metadata.id);
    return preference == nullptr ? metadata.enabled_by_default : preference->enabled;
}

std::uint32_t app_duration(const Settings& settings, const AppMetadata& metadata) {
    const auto* preference = find_app_preference(settings, metadata.id);
    return preference == nullptr ? metadata.default_duration_seconds
                                 : preference->duration_seconds;
}

cJSON* settings_to_json(const Settings& settings) {
    auto* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "auto_rotate", settings.auto_rotate);
    cJSON_AddNumberToObject(json, "brightness", settings.brightness_percent);
    cJSON_AddNumberToObject(json, "rotation", settings.rotation_degrees);
    cJSON_AddStringToObject(json, "timezone", settings.timezone.data());

    auto* apps = cJSON_AddArrayToObject(json, "apps");
    for (std::size_t index = 0; index < app_registry->size(); ++index) {
        const auto* app = app_registry->at(index);
        if (app == nullptr) {
            continue;
        }
        const auto& metadata = app->metadata();
        auto* entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "id", metadata.id);
        cJSON_AddStringToObject(entry, "title", metadata.title);
        cJSON_AddBoolToObject(entry, "rotatable", metadata.rotatable);
        cJSON_AddBoolToObject(entry, "enabled", app_enabled(settings, metadata));
        cJSON_AddNumberToObject(entry, "duration", app_duration(settings, metadata));
        cJSON_AddItemToArray(apps, entry);
    }

    auto* schedules = cJSON_AddArrayToObject(json, "schedules");
    for (std::size_t index = 0; index < settings.schedule_rule_count; ++index) {
        const auto& rule = settings.schedule_rules[index];
        auto* entry = cJSON_CreateObject();
        cJSON_AddBoolToObject(entry, "enabled", rule.enabled);
        cJSON_AddNumberToObject(entry, "days", rule.weekday_mask);
        cJSON_AddNumberToObject(entry, "start", rule.start_minute);
        cJSON_AddNumberToObject(entry, "end", rule.end_minute);
        cJSON_AddStringToObject(entry, "app", rule.app_id.data());
        cJSON_AddItemToArray(schedules, entry);
    }
    return json;
}

cJSON* status_to_json() {
    const auto network = network::status();
    auto* json = cJSON_CreateObject();

    auto* wifi = cJSON_AddObjectToObject(json, "wifi");
    cJSON_AddStringToObject(wifi, "state", network::state_name(network.state));
    cJSON_AddStringToObject(wifi, "ssid", network.ssid.data());
    cJSON_AddStringToObject(wifi, "ip", network.ip.data());
    cJSON_AddNumberToObject(wifi, "rssi", network.rssi);
    cJSON_AddBoolToObject(wifi, "ap_active", network.ap_active);
    cJSON_AddStringToObject(wifi, "ap_ssid", network.ap_ssid.data());
    cJSON_AddStringToObject(wifi, "ap_ip", network.ap_ip.data());

    const auto* app = esp_app_get_description();
    cJSON_AddStringToObject(json, "version", app->version);
    cJSON_AddStringToObject(json, "build", app->date);
    const auto* running = esp_ota_get_running_partition();
    cJSON_AddStringToObject(json, "partition", running != nullptr ? running->label : "?");
    cJSON_AddNumberToObject(json, "uptime_s",
                            static_cast<double>(esp_timer_get_time() / 1'000'000));
    cJSON_AddNumberToObject(json, "free_internal_kb",
                            heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    cJSON_AddNumberToObject(json, "free_psram_kb",
                            heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);

    cJSON_AddBoolToObject(json, "time_synced", network.time_synced);
    cJSON_AddStringToObject(json, "time_source",
                            network.time_synced        ? "sntp"
                            : network.time_from_client ? "browser"
                                                       : "none");
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char time_text[32]{};
    std::strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", &local);
    cJSON_AddStringToObject(json, "time", time_text);
    return json;
}

const char* get_string(const cJSON* object, const char* key) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

bool get_number(const cJSON* object, const char* key, double& value) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    value = item->valuedouble;
    return true;
}

bool get_bool(const cJSON* object, const char* key, bool& value) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsBool(item)) {
        return false;
    }
    value = cJSON_IsTrue(item);
    return true;
}

// Áp các field có trong JSON lên settings; field vắng mặt giữ nguyên.
bool apply_json(const cJSON* json, Settings& settings, const char*& error) {
    bool flag = false;
    double number = 0;
    if (get_bool(json, "auto_rotate", flag)) {
        settings.auto_rotate = flag;
    }
    if (get_number(json, "brightness", number)) {
        if (number < 1 || number > 100) {
            error = "brightness must be 1-100";
            return false;
        }
        settings.brightness_percent = static_cast<std::uint8_t>(number);
    }
    if (get_number(json, "rotation", number)) {
        if (!is_valid_rotation(static_cast<std::uint16_t>(number))) {
            error = "rotation must be 0, 90, 180 or 270";
            return false;
        }
        settings.rotation_degrees = static_cast<std::uint16_t>(number);
    }
    if (const char* timezone = get_string(json, "timezone"); timezone != nullptr) {
        if (timezone[0] == '\0' || std::strlen(timezone) >= kTimezoneSize) {
            error = "invalid timezone";
            return false;
        }
        assign_text(settings.timezone, timezone);
    }

    const auto* apps = cJSON_GetObjectItemCaseSensitive(json, "apps");
    if (cJSON_IsArray(apps)) {
        const cJSON* entry = nullptr;
        cJSON_ArrayForEach(entry, apps) {
            const char* id = get_string(entry, "id");
            const auto* app = id == nullptr ? nullptr : app_registry->find(id);
            if (app == nullptr) {
                error = "unknown app id";
                return false;
            }
            const auto& metadata = app->metadata();
            auto* preference = ensure_app_preference(settings, metadata.id,
                                                     metadata.enabled_by_default,
                                                     metadata.default_duration_seconds);
            if (preference == nullptr) {
                error = "too many apps";
                return false;
            }
            if (get_bool(entry, "enabled", flag)) {
                preference->enabled = flag;
            }
            if (get_number(entry, "duration", number)) {
                if (number < 5 || number > 86'400) {
                    error = "duration must be 5-86400 seconds";
                    return false;
                }
                preference->duration_seconds = static_cast<std::uint32_t>(number);
            }
        }
    }

    const auto* schedules = cJSON_GetObjectItemCaseSensitive(json, "schedules");
    if (cJSON_IsArray(schedules)) {
        if (static_cast<std::size_t>(cJSON_GetArraySize(schedules)) > kMaxScheduleRules) {
            error = "too many schedules";
            return false;
        }
        settings.schedule_rules = {};
        settings.schedule_rule_count = 0;
        const cJSON* entry = nullptr;
        cJSON_ArrayForEach(entry, schedules) {
            auto& rule = settings.schedule_rules[settings.schedule_rule_count++];
            double days = 0;
            double start = 0;
            double end = 0;
            const char* app_id = get_string(entry, "app");
            if (!get_number(entry, "days", days) || !get_number(entry, "start", start) ||
                !get_number(entry, "end", end) || app_id == nullptr ||
                app_registry->find(app_id) == nullptr || days < 0 || days > 0x7F ||
                start < 0 || start >= 1'440 || end < 0 || end >= 1'440) {
                error = "invalid schedule";
                return false;
            }
            rule.enabled = !get_bool(entry, "enabled", flag) || flag;
            rule.weekday_mask = static_cast<std::uint8_t>(days);
            rule.start_minute = static_cast<std::uint16_t>(start);
            rule.end_minute = static_cast<std::uint16_t>(end);
            rule.mode = ScheduleMode::force;
            assign_text(rule.app_id, app_id);
        }
    }

    if (!validate_settings(settings)) {
        error = "settings rejected";
        return false;
    }
    return true;
}

esp_err_t index_handler(httpd_req_t* request) {
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    // EMBED_TXTFILES thêm '\0' ở cuối, không gửi byte đó.
    return httpd_resp_send(request, index_html_start,
                           static_cast<ssize_t>(index_html_end - index_html_start - 1));
}

esp_err_t state_handler(httpd_req_t* request) {
    Settings snapshot{};
    {
        std::lock_guard lock(settings_mutex);
        if (!has_published) {
            return send_error(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Not ready");
        }
        snapshot = has_pending ? pending : published;
    }
    JsonPtr json(cJSON_CreateObject());
    cJSON_AddItemToObject(json.get(), "settings", settings_to_json(snapshot));
    cJSON_AddItemToObject(json.get(), "status", status_to_json());
    return send_json(request, json.get());
}

esp_err_t settings_handler(httpd_req_t* request) {
    const auto json = read_json_body(request);
    if (!json) {
        return send_error(request, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    std::lock_guard lock(settings_mutex);
    if (!has_published) {
        return send_error(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Not ready");
    }
    Settings updated = has_pending ? pending : published;
    const char* error = nullptr;
    if (!apply_json(json.get(), updated, error)) {
        return send_error(request, HTTPD_400_BAD_REQUEST, error);
    }
    pending = updated;
    has_pending = true;
    return send_ok(request);
}

esp_err_t wifi_handler(httpd_req_t* request) {
    const auto json = read_json_body(request);
    const char* ssid = json ? get_string(json.get(), "ssid") : nullptr;
    const char* password = json ? get_string(json.get(), "password") : nullptr;
    if (ssid == nullptr) {
        return send_error(request, HTTPD_400_BAD_REQUEST, "ssid is required");
    }
    const auto ssid_length = std::strlen(ssid);
    const auto password_length = password == nullptr ? 0 : std::strlen(password);
    if (ssid_length == 0 || ssid_length > 32 || password_length > 64 ||
        (password_length > 0 && password_length < 8)) {
        return send_error(request, HTTPD_400_BAD_REQUEST,
                          "SSID 1-32 chars, password empty or 8-64 chars");
    }
    // Trả lời trước khi đổi mạng: client có thể đang nối qua chính STA này.
    send_ok(request);
    const auto result = network::set_credentials(ssid, password == nullptr ? "" : password);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Saving Wi-Fi failed: %s", esp_err_to_name(result));
    }
    return ESP_OK;
}

// Trình duyệt gửi giờ của nó ({"unix_ms": Date.now()}) khi thiết bị chưa
// đồng bộ SNTP, ví dụ lúc điện thoại đang nối vào AP cấu hình không có internet.
esp_err_t time_handler(httpd_req_t* request) {
    const auto json = read_json_body(request);
    double unix_ms = 0;
    // Chặn giá trị vô lý: phải sau 2024-01-01.
    if (!json || !get_number(json.get(), "unix_ms", unix_ms) || unix_ms < 1'704'067'200'000.0) {
        return send_error(request, HTTPD_400_BAD_REQUEST, "unix_ms is required");
    }
    if (network::status().time_synced) {
        return send_ok(request);
    }
    network::set_time_from_client(static_cast<std::int64_t>(unix_ms));
    return send_ok(request);
}

esp_err_t reboot_handler(httpd_req_t* request) {
    schedule_restart();
    return send_ok(request);
}

// Nhận firmware .bin thô (body = file) và ghi vào OTA slot còn lại. Image mới
// chỉ được xác nhận khi boot lên và tự kiểm tra display thành công; nếu không,
// bootloader rollback về bản đang chạy.
esp_err_t ota_handler(httpd_req_t* request) {
    const auto* partition = esp_ota_get_next_update_partition(nullptr);
    if (partition == nullptr) {
        return send_error(request, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
    }
    if (request->content_len == 0 || request->content_len > partition->size) {
        return send_error(request, HTTPD_400_BAD_REQUEST, "Firmware size invalid");
    }

    std::unique_ptr<char[]> buffer(new (std::nothrow) char[kOtaChunkSize]);
    if (!buffer) {
        return send_error(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    esp_ota_handle_t handle{};
    auto result = esp_ota_begin(partition, request->content_len, &handle);
    if (result != ESP_OK) {
        return send_error(request, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(result));
    }
    ESP_LOGI(kTag, "OTA upload %u bytes -> %s", static_cast<unsigned>(request->content_len),
             partition->label);

    std::size_t received = 0;
    while (received < request->content_len) {
        const auto wanted = std::min(kOtaChunkSize, request->content_len - received);
        const int count = httpd_req_recv(request, buffer.get(), wanted);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (count <= 0) {
            esp_ota_abort(handle);
            ESP_LOGE(kTag, "OTA upload interrupted at %u bytes", static_cast<unsigned>(received));
            return ESP_FAIL;
        }
        if ((result = esp_ota_write(handle, buffer.get(), static_cast<std::size_t>(count))) !=
            ESP_OK) {
            esp_ota_abort(handle);
            return send_error(request, HTTPD_400_BAD_REQUEST, esp_err_to_name(result));
        }
        received += static_cast<std::size_t>(count);
    }

    if ((result = esp_ota_end(handle)) != ESP_OK) {
        return send_error(request, HTTPD_400_BAD_REQUEST,
                          result == ESP_ERR_OTA_VALIDATE_FAILED ? "Image validation failed"
                                                                : esp_err_to_name(result));
    }
    if ((result = esp_ota_set_boot_partition(partition)) != ESP_OK) {
        return send_error(request, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(result));
    }
    ESP_LOGI(kTag, "OTA written, rebooting into %s", partition->label);
    schedule_restart();
    return send_ok(request);
}

void restart_cb(void*) {
    esp_restart();
}

esp_err_t register_uri(const char* uri, const httpd_method_t method,
                       esp_err_t (*handler)(httpd_req_t*)) {
    httpd_uri_t route{};
    route.uri = uri;
    route.method = method;
    route.handler = handler;
    return httpd_register_uri_handler(server, &route);
}

}  // namespace

esp_err_t start(const AppRegistry& registry) noexcept {
    if (server != nullptr) {
        return ESP_OK;
    }
    app_registry = &registry;

    esp_timer_create_args_t timer_args{};
    timer_args.callback = restart_cb;
    timer_args.name = "web_restart";
    auto result = esp_timer_create(&timer_args, &restart_timer);
    if (result != ESP_OK) {
        return result;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8 * 1024;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 15;
    if ((result = httpd_start(&server, &config)) != ESP_OK) {
        return result;
    }

    if ((result = register_uri("/", HTTP_GET, index_handler)) != ESP_OK ||
        (result = register_uri("/api/state", HTTP_GET, state_handler)) != ESP_OK ||
        (result = register_uri("/api/settings", HTTP_POST, settings_handler)) != ESP_OK ||
        (result = register_uri("/api/wifi", HTTP_POST, wifi_handler)) != ESP_OK ||
        (result = register_uri("/api/time", HTTP_POST, time_handler)) != ESP_OK ||
        (result = register_uri("/api/reboot", HTTP_POST, reboot_handler)) != ESP_OK ||
        (result = register_uri("/api/ota", HTTP_POST, ota_handler)) != ESP_OK) {
        return result;
    }
    ESP_LOGI(kTag, "Web settings server started on port %d", config.server_port);
    return ESP_OK;
}

void publish_settings(const Settings& settings) noexcept {
    std::lock_guard lock(settings_mutex);
    published = settings;
    has_published = true;
}

bool take_settings_update(Settings& settings) noexcept {
    std::lock_guard lock(settings_mutex);
    if (!has_pending) {
        return false;
    }
    settings = pending;
    published = pending;
    has_pending = false;
    return true;
}

}  // namespace apphub::web
