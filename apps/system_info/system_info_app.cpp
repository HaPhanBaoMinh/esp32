#include "apps/system_info_app.hpp"

#include <cstdio>

#include "board/board.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "network/network_service.hpp"
#include "ui/ui_shell.hpp"

namespace apphub::apps {
namespace {

constexpr char kTag[] = "app.system_info";
constexpr AppMetadata kMetadata{
    .id = "system_info",
    .title = "System Info",
    .rotatable = true,
    .enabled_by_default = true,
    .default_duration_seconds = 60,
};

}  // namespace

const AppMetadata& SystemInfoApp::metadata() const noexcept {
    return kMetadata;
}

void SystemInfoApp::on_enter() {
    const auto info = board::probe();
    ESP_LOGI(kTag, "enter: board=%s flash=%uMB psram=%uMB", info.variant,
             static_cast<unsigned>(info.flash_bytes / (1024 * 1024)),
             static_cast<unsigned>(info.psram_bytes / (1024 * 1024)));
    last_update_ms_ = 0;
    ui::set_title("System Info");
    ui::set_page_indicator("auto");
    if (ui::content_root() == nullptr) {
        return;
    }

    root_ = lv_obj_create(ui::content_root());
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(root_);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x0B1019), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 14, 0);
    lv_obj_set_scrollable(root_, false);

    auto* badge = lv_label_create(root_);
    lv_label_set_text(badge, "LCDWiki ES3C28P");
    lv_obj_set_style_text_color(badge, lv_color_hex(0x65D4A6), 0);
    lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 0, 0);

    details_label_ = lv_label_create(root_);
    lv_obj_set_width(details_label_, LV_PCT(100));
    lv_obj_set_style_text_color(details_label_, lv_color_hex(0xD5DDE8), 0);
    lv_obj_set_style_text_line_space(details_label_, 3, 0);
    lv_obj_align(details_label_, LV_ALIGN_TOP_LEFT, 0, 26);
    on_tick(1);
}

void SystemInfoApp::on_exit() {
    ESP_LOGI(kTag, "exit");
    if (root_ != nullptr) {
        lv_obj_delete(root_);
    }
    root_ = nullptr;
    details_label_ = nullptr;
}

void SystemInfoApp::on_tick(const std::uint64_t now_ms) {
    if (details_label_ == nullptr ||
        (last_update_ms_ != 0 && now_ms - last_update_ms_ < 1'000)) {
        return;
    }
    last_update_ms_ = now_ms;

    const auto info = board::probe();
    const auto free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    const auto free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;

    // Hiện địa chỉ web settings, hoặc thông tin AP cấu hình khi chưa có Wi-Fi.
    const auto network = network::status();
    char network_text[128]{};
    if (network.state == network::State::connected) {
        std::snprintf(network_text, sizeof(network_text), "Wi-Fi: %s (%d dBm)\nWeb: http://%s",
                      network.ssid.data(), static_cast<int>(network.rssi), network.ip.data());
    } else if (network.ap_active) {
        std::snprintf(network_text, sizeof(network_text),
                      "Setup Wi-Fi: %s\nPass: %s\nWeb: http://%s", network.ap_ssid.data(),
                      network::kSetupApPassword, network.ap_ip.data());
    } else {
        std::snprintf(network_text, sizeof(network_text), "Wi-Fi: connecting to %s",
                      network.ssid.data());
    }

    char text[320]{};
    std::snprintf(text, sizeof(text),
                  "%s\nChip: ESP32-S3 rev %u\nFlash: %u MB  PSRAM: %u MB\n"
                  "Free: %u KB + %u KB PSRAM\nUptime: %llu s",
                  network_text, static_cast<unsigned>(info.chip_revision),
                  static_cast<unsigned>(info.flash_bytes / (1024 * 1024)),
                  static_cast<unsigned>(info.psram_bytes / (1024 * 1024)),
                  static_cast<unsigned>(free_internal), static_cast<unsigned>(free_psram),
                  static_cast<unsigned long long>(now_ms / 1'000));
    lv_label_set_text(details_label_, text);
}

}  // namespace apphub::apps

