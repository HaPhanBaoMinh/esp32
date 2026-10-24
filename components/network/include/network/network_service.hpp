#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "esp_err.h"

namespace apphub::network {

enum class State : std::uint8_t {
    stopped,
    connecting,   // Có Wi-Fi đã lưu, đang kết nối.
    connected,    // Đã có IP từ router.
    setup_ap,     // Chưa kết nối được: phát AP để cấu hình qua web.
};

struct Status {
    State state{State::stopped};
    bool ap_active{false};
    std::int8_t rssi{0};
    std::array<char, 33> ssid{};      // Mạng STA đã lưu.
    std::array<char, 16> ip{};        // IP STA khi connected.
    std::array<char, 33> ap_ssid{};   // AP cấu hình, ví dụ AppHub-1A2B.
    std::array<char, 16> ap_ip{};
    bool time_synced{false};   // Giờ lấy từ SNTP (internet).
    bool time_from_client{false};  // Giờ do trình duyệt gửi lên khi chưa có SNTP.
};

// AP cấu hình dùng WPA2 với mật khẩu cố định này; chỉ bật khi chưa có Wi-Fi.
inline constexpr char kSetupApPassword[] = "apphub123";
inline constexpr char kHostname[] = "apphub";

// Yêu cầu nvs_flash_init() đã chạy. Thông tin Wi-Fi nằm trong namespace NVS
// riêng ("wifi"), không nằm trong Settings blob và không trả ra qua API.
esp_err_t start() noexcept;
Status status() noexcept;
const char* state_name(State state) noexcept;

// Đặt giờ hệ thống từ nguồn bên ngoài (trình duyệt). Dùng khi chưa có internet;
// SNTP vẫn ghi đè khi đồng bộ được.
void set_time_from_client(std::int64_t unix_ms) noexcept;

// Lưu Wi-Fi mới vào NVS và kết nối lại ngay.
esp_err_t set_credentials(std::string_view ssid, std::string_view password) noexcept;

}  // namespace apphub::network
