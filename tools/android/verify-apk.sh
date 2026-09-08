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
    signature_output=$("$apksigner_path" verify --verbose --print-certs "$apk_path")
    printf '%s\n' "$signature_output"
    certificate_sha256=$(printf '%s\n' "$signature_output" \
        | sed -n \
            -e 's/^[[:space:]]*Signer #1 certificate SHA-256 digest: //p' \
            -e 's/^[[:space:]]*V[0-9.]* Signer: certificate SHA-256 digest: //p' \
        | head -n 1 \
        | tr '[:upper:]' '[:lower:]' \
        | tr -d ':')

    if [[ -n "${EXPECTED_CERT_SHA256:-}" ]]; then
        expected_certificate_sha256=$(printf '%s' "$EXPECTED_CERT_SHA256" \
            | tr '[:upper:]' '[:lower:]' \
            | tr -d ':')
        if [[ -z "$certificate_sha256" || "$certificate_sha256" != "$expected_certificate_sha256" ]]; then
            echo "Unexpected APK signing certificate: ${certificate_sha256:-not-found}" >&2
            exit 1
        fi
    fi
elif [[ -n "${EXPECTED_CERT_SHA256:-}" ]]; then
    echo "VERIFY_APK_SIGNATURE=1 is required when EXPECTED_CERT_SHA256 is set" >&2
    exit 1
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

printf 'Verified %s: arm64-v8a only, CemuAndroid native library present, application ID %s, versionCode %s, certificate SHA-256 %s\n' \
    "$apk_path" \
    "${EXPECTED_APPLICATION_ID:-not-checked}" \
    "${version_code:-not-checked}" \
    "${certificate_sha256:-not-checked}"
