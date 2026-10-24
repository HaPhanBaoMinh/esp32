# ESP32 App Hub

Firmware cho ESP32-S3 màn hình cảm ứng, tổ chức theo mô hình nhiều app dùng chung
một màn hình. App đang hiển thị được chọn bởi lịch, thao tác người dùng hoặc bộ
luân phiên tự động.

## Trạng thái hiện tại

- Đã nhận dạng ESP32-S3 revision 0.2, flash 16 MB, PSRAM 8 MB.
- Đã có lõi quản lý app, rotation policy, lịch theo ngày/giờ và NVS settings.
- Đã dành hai OTA slot 5 MB, hỗ trợ rollback.
- Đã xác định board LCDWiki ES3C28P và thêm driver ILI9341V + FT6336G theo
  schematic chính hãng.
- Đã có LVGL shell, giao diện Clock/System Info và Settings cảm ứng cơ bản.
- Màn hình hỗ trợ dọc và ngang (0/90/180/270°), touch xoay theo.
- Wi-Fi, SNTP, mDNS `apphub.local` và trang web cấu hình.
- Cập nhật firmware qua Wi-Fi (upload trên web hoặc `tools/ota_upload.sh`).
- Firmware đã chạy trên board thật.

## Kiến trúc

```text
main/                  Composition root và vòng lặp firmware
components/app_core/   App registry, lifecycle, rotation và schedule
components/board/      Adapter duy nhất phụ thuộc display/touch/GPIO
components/ui/         LVGL shell, header/footer và điều hướng cảm ứng
components/settings/   Lưu cấu hình có version vào NVS
components/ota/        HTTPS OTA và xác nhận/rollback image
components/network/    Wi-Fi STA, AP cấu hình, SNTP và mDNS
components/web/        HTTP server, REST API, trang cấu hình và OTA upload
apps/clock/            App đồng hồ mẫu
apps/system_info/      App chẩn đoán phần cứng mẫu
apps/settings_app/     Menu cấu hình lưu bằng NVS
tests/                 Unit test chạy trên máy, không cần board
```

Luật chọn app có thứ tự ưu tiên: overlay (Settings) → lịch ép app → app được
ghim tạm → rotation bình thường. Settings mặc định chuyển app mỗi 60 giây và
có rule Clock từ 22:00 đến 07:00.

## Build

```bash
source /home/baominh/.local/share/espressif/esp-idf-v6.1/export.sh
idf.py set-target esp32s3
idf.py build
```

Unit test phần lõi:

```bash
cmake -S tests -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

Flash lần đầu và xem log kiểm thử touch:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

## Wi-Fi và web cấu hình

Lần đầu (chưa có Wi-Fi), thiết bị phát AP `AppHub-XXXX`, mật khẩu `apphub123`.
Kết nối vào AP đó, mở `http://192.168.4.1`, nhập Wi-Fi nhà ở mục Wi-Fi. Khi
đã có mạng, mở `http://apphub.local` hoặc IP hiện trong app System Info.
Nếu Wi-Fi đã lưu không kết nối được sau 20 giây, AP cấu hình tự bật lại.

Trang web chỉnh được: độ sáng, hướng màn hình, tự chuyển app, bật/tắt và
thời lượng từng app, lịch hẹn giờ ép hiển thị một app, múi giờ, Wi-Fi, khởi
động lại và upload firmware.

REST API dùng cho script:

| Method | Path            | Nội dung                                        |
|--------|-----------------|-------------------------------------------------|
| GET    | `/api/state`    | `{settings, status}`                            |
| POST   | `/api/settings` | JSON một phần của `settings`, ví dụ `{"rotation":90}` |
| POST   | `/api/wifi`     | `{"ssid": "...", "password": "..."}`            |
| POST   | `/api/ota`      | Body là file `.bin`, ghi vào OTA slot còn lại   |
| POST   | `/api/time`     | `{"unix_ms": ...}`, đặt giờ khi chưa có SNTP     |
| POST   | `/api/reboot`   |                                                 |

Web chưa có đăng nhập: ai trong cùng mạng LAN cũng mở được và có thể nạp
firmware. Chỉ dùng trong mạng tin cậy.

## Thời gian

Board không có RTC giữ giờ khi mất điện. Lúc boot giờ được lấy theo thứ tự:

1. SNTP (`pool.ntp.org`) ngay khi có Wi-Fi có internet.
2. Nếu chưa có internet, mở trang web cấu hình (kể cả qua AP `AppHub-XXXX`),
   trình duyệt sẽ tự gửi giờ của nó lên `/api/time`.

Chưa có nguồn nào thì Clock hiện `Waiting for time sync`.

## Cập nhật firmware qua Wi-Fi

```bash
idf.py build
tools/ota_upload.sh                 # mặc định apphub.local
tools/ota_upload.sh 192.168.1.50    # hoặc chỉ định IP
```

Image mới chỉ được xác nhận khi display/touch khởi tạo thành công. Nếu bản mới
crash hoặc reset trước đó, bootloader tự quay về bản cũ.

## Thêm app

1. Sao chép một thư mục trong `apps/`.
2. Implement `apphub::App`.
3. Thêm component vào `main/CMakeLists.txt` và đăng ký instance trong
   `main/main.cpp`.
4. Thêm cấu hình mặc định của app trong `make_default_settings()`.

Không đặt Wi-Fi password, Spotify token hoặc API key trong Git. Các giá trị đó
sẽ được provision và lưu trong NVS.

Thiết kế chi tiết, thứ tự ưu tiên và kế hoạch triển khai nằm trong
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). Quy trình xác định driver màn
hình nằm trong [`docs/BOARD_BRINGUP.md`](docs/BOARD_BRINGUP.md).
