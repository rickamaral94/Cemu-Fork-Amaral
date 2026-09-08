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

find_android_tool() {
    local tool_name=$1

    if command -v "$tool_name" >/dev/null 2>&1; then
        command -v "$tool_name"
        return
    fi

    if [[ -n "${ANDROID_HOME:-}" && -d "$ANDROID_HOME" ]]; then
        find "$ANDROID_HOME" -type f -name "$tool_name" -perm -u+x -print \
            | sort -V \
            | tail -n 1
    fi
}

if [[ "${VERIFY_APK_SIGNATURE:-0}" == "1" ]]; then
    apksigner_path=$(find_android_tool apksigner)
    if [[ -z "$apksigner_path" ]]; then
        echo "apksigner is required to verify the APK signature" >&2
        exit 1
    fi
    "$apksigner_path" verify --verbose "$apk_path"
fi

if [[ -n "${EXPECTED_APPLICATION_ID:-}" ]]; then
    apkanalyzer_path=$(find_android_tool apkanalyzer)
    if [[ -z "$apkanalyzer_path" ]]; then
        echo "apkanalyzer is required to verify the application ID" >&2
        exit 1
    fi

    application_id=$("$apkanalyzer_path" manifest application-id "$apk_path")
    if [[ "$application_id" != "$EXPECTED_APPLICATION_ID" ]]; then
        echo "Unexpected application ID: $application_id" >&2
        exit 1
    fi

    version_code=$("$apkanalyzer_path" manifest version-code "$apk_path")
    if [[ ! "$version_code" =~ ^[1-9][0-9]*$ ]]; then
        echo "Invalid Android versionCode: $version_code" >&2
        exit 1
    fi
fi

printf 'Verified %s: arm64-v8a only, CemuAndroid native library present, application ID %s, versionCode %s\n' \
    "$apk_path" \
    "${EXPECTED_APPLICATION_ID:-not-checked}" \
    "${version_code:-not-checked}"
