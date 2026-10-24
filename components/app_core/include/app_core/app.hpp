#pragma once

#include <cstdint>

namespace apphub {

struct AppMetadata {
    const char* id;
    const char* title;
    bool rotatable;
    bool enabled_by_default;
    std::uint32_t default_duration_seconds;
};

class App {
public:
    virtual ~App() = default;

    virtual const AppMetadata& metadata() const noexcept = 0;

    // App tạo view trong on_enter và giải phóng view trong on_exit. Dịch vụ dữ
    // liệu nên sống bên ngoài view để không mất cache mỗi khi chuyển app.
    virtual void on_enter() = 0;
    virtual void on_exit() = 0;
    virtual void on_tick(std::uint64_t now_ms) = 0;
};

}  // namespace apphub

