#!/usr/bin/env bash
# Cập nhật firmware qua Wi-Fi: tools/ota_upload.sh [host] [file.bin]
set -euo pipefail

host="${1:-apphub.local}"
image="${2:-build/esp32_app_hub.bin}"

if [[ ! -f "$image" ]]; then
    echo "Không thấy $image, hãy chạy idf.py build trước" >&2
    exit 1
fi

echo "Upload $image ($(stat -c %s "$image") bytes) -> http://$host/api/ota"
curl --fail-with-body --progress-bar \
    -H "Content-Type: application/octet-stream" \
    --data-binary "@$image" "http://$host/api/ota"
echo
echo "Thiết bị đang khởi động lại vào firmware mới."
