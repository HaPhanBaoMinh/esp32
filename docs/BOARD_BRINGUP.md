# Board bring-up: LCDWiki ES3C28P

Thông tin đọc trực tiếp từ chip:

- ESP32-S3 QFN56 revision 0.2
- 16 MB quad SPI flash
- 8 MB embedded PSRAM
- USB Serial/JTAG tại `/dev/ttyACM0`

Thông tin xác nhận từ ảnh board và tài liệu LCDWiki:

- SKU: ES3C28P (bản capacitive touch)
- LCD: ILI9341V, SPI 4-wire, 240x320
- Touch: FT6336G/FT5x06 family, I2C address `0x38`
- LCD: CS=10, DC=46, SCLK=12, MOSI=11, MISO=13, BL=45
- Touch: SDA=16, SCL=15, RST=18, INT=17
- Hướng dọc cần MADCTL `MX|BGR` (`rotation.mirror_x = true` trong
  `ui_shell.cpp`). Thiếu mirror thì chữ bị ngược như soi gương.

Bring-up được kiểm tra theo thứ tự:

1. Backlight ở độ sáng thấp.
2. Display init và test các màu đỏ/xanh lá/xanh dương.
3. Touch raw coordinates ở bốn góc.
4. Rotation/mirror và mapping touch.
5. LVGL double buffer DMA-capable trong internal RAM.
6. Test liên tục 30 phút trước khi phát hành OTA production.

Nguồn phần cứng chính thức:

- [Trang sản phẩm LCDWiki ES3C28P](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display)
- [Hướng dẫn demo ESP-IDF của LCDWiki](https://www.lcdwiki.com/res/ES3C28P/2.8inch_ES3C28P_ES3N28P_ESP-IDF_Demo_Instructions.pdf)
- [Schematic LCDWiki](https://www.lcdwiki.com/res/ES3C28P/2.8inch_ESP32-S3_Display_Schematic.pdf)

