#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <apk>" >&2
    exit 2
fi

apk_path=$1
if [[ ! -f "$apk_path" ]]; then
    echo "APK not found: $apk_path" >&2
    exit 1
fi

mapfile -t native_libraries < <(
    unzip -Z1 "$apk_path" | grep -E '^lib/[^/]+/[^/]+\.so$' || true
)

if [[ ${#native_libraries[@]} -eq 0 ]]; then
    echo "APK contains no native libraries" >&2
    exit 1
fi

mapfile -t abis < <(
    printf '%s\n' "${native_libraries[@]}" | cut -d/ -f2 | sort -u
)

if [[ ${#abis[@]} -ne 1 || "${abis[0]}" != "arm64-v8a" ]]; then
    echo "APK must contain only arm64-v8a; found: ${abis[*]}" >&2
    exit 1
fi

if ! unzip -Z1 "$apk_path" | grep -qx 'lib/arm64-v8a/libCemuAndroid.so'; then
    echo "APK is missing lib/arm64-v8a/libCemuAndroid.so" >&2
    exit 1
fi

echo "Verified $apk_path: arm64-v8a only, CemuAndroid native library present"
