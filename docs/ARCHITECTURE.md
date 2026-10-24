# Kiến trúc App Hub

## Mục tiêu

Firmware được chia thành ba lớp để phần cứng, hạ tầng và từng app không phụ
thuộc chặt vào nhau:

```text
Board/UI       display + touch + LVGL + backlight
Platform       Wi-Fi + time + NVS + OTA + HTTP services
Applications   clock + Spotify + gold + settings + app mới
```

Mỗi app implement `apphub::App` và chỉ sở hữu màn hình/view của nó. Dữ liệu có
thể dùng lại (Spotify session, giá vàng cache, trạng thái mạng) nên nằm trong
service dùng chung, không nằm trong view. Vì vậy chuyển app không làm đăng nhập
hoặc tải dữ liệu lại từ đầu.

## Luật chọn app

`DisplayPolicy` chạy theo thứ tự ưu tiên cố định:

1. Overlay hệ thống, ví dụ Settings hoặc hộp thoại cảnh báo.
2. Schedule `force`, ví dụ 22:00–07:00 chỉ hiện Clock.
3. App người dùng ghim tạm thời.
4. Danh sách app được bật trong vòng xoay.

Rule qua nửa đêm được gắn với ngày bắt đầu. Ví dụ rule Thứ Sáu 22:00–07:00
vẫn đúng lúc 02:00 sáng Thứ Bảy. Nếu nhiều rule `force` cùng khớp, rule nằm
trước trong danh sách thắng.

## Settings dự kiến trên màn hình

- Apps: bật/tắt app, sắp xếp thứ tự, thời lượng riêng cho từng app.
- Display: độ sáng, kiểu/chuyển động khi đổi app, thời gian tắt màn hình.
- Schedule: ngày trong tuần, giờ bắt đầu/kết thúc, app bị ép hiển thị.
- Network: trạng thái Wi-Fi, đổi mạng, IP và chất lượng sóng.
- Time: múi giờ, máy chủ NTP và trạng thái đồng bộ.
- Integrations: Spotify login/logout, nguồn giá vàng và chu kỳ refresh.
- Update: phiên bản hiện tại, kiểm tra/cài OTA và kênh stable/beta.
- System: ngôn ngữ, khởi động lại, export/import setting, factory reset.

Không ghi secret vào source hoặc log. Token OAuth cần lưu riêng với setting
không nhạy cảm; trước khi phát hành nên bật NVS encryption và Secure Boot.

## OTA

Partition table có hai app slot 5 MB. Image mới được tải bằng HTTPS vào slot
không hoạt động, reboot sang image mới và chỉ được xác nhận sau khi self-test
phần cứng thành công. Nếu reset/crash trước khi xác nhận, bootloader rollback về
image trước.

Đang dùng: upload file `.bin` lên `POST /api/ota` (trang web hoặc
`tools/ota_upload.sh`). Dự kiến thêm: thiết bị định kỳ đọc manifest có chữ ký
từ server và tự pull bản mới.

Firmware đầu tiên vẫn phải flash bằng USB. Sau đó có thể cập nhật qua Wi-Fi.

## Trạng thái triển khai

- [x] App registry và lifecycle.
- [x] Rotation, pin, overlay và schedule qua nửa đêm.
- [x] Settings có schema/version, kiểm tra dữ liệu và lưu NVS.
- [x] HTTPS OTA, hai slot và rollback confirmation gate.
- [x] Clock/System Info app mẫu và host unit tests.
- [x] Driver ES3C28P cho display, touch và PWM backlight.
- [x] LVGL shell, Clock/System Info và Settings UI cơ bản.
- [x] Hướng màn hình dọc/ngang, layout shell tự co giãn.
- [x] Wi-Fi STA + AP cấu hình, reconnect, SNTP và mDNS.
- [x] Web cấu hình, REST API và OTA upload qua trình duyệt.
- [ ] Spotify OAuth/controls và gold data provider.
- [ ] Đăng nhập cho web cấu hình.
- [ ] OTA manifest, ký firmware và màn hình tiến trình.
