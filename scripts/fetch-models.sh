#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dest="${1:-$here/../models}"
base="https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/master"

mkdir -p "$dest"

fetch() {
    local remote="$1" name="$2"
    if [[ -s "$dest/$name" ]]; then
        echo "skip  $name"
        return
    fi
    echo "fetch $name"
    curl -fL --retry 3 --retry-delay 2 -o "$dest/$name" "$base/$remote"
}

fetch "onnx/PP-OCRv5/det/ch_PP-OCRv5_det_mobile.onnx" "ch_PP-OCRv5_det_mobile.onnx"
fetch "onnx/PP-OCRv5/rec/ch_PP-OCRv5_rec_mobile.onnx" "ch_PP-OCRv5_rec_mobile.onnx"
fetch "paddle/PP-OCRv5/rec/ch_PP-OCRv5_rec_mobile/ppocrv5_dict.txt" "ppocrv5_dict.txt"

echo "models ready in $dest"
