#!/usr/bin/env bash

set -euo pipefail

readonly PACKAGE_ID="io.github.rickamaral94.cemu"
readonly MAIN_COMPONENT="$PACKAGE_ID/info.cemu.cemu.MainActivity"

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: $0 <apk> [output-directory]" >&2
    exit 2
fi

apk_path=$1
if [[ ! -f "$apk_path" ]]; then
    echo "APK not found: $apk_path" >&2
    exit 1
fi

if ! command -v adb >/dev/null 2>&1; then
    echo "adb is required for the physical-device smoke test" >&2
    exit 1
fi

if [[ -z "${ANDROID_SERIAL:-}" ]]; then
    mapfile -t connected_devices < <(adb devices | awk 'NR > 1 && $2 == "device" { print $1 }')
    if [[ ${#connected_devices[@]} -ne 1 ]]; then
        echo "Expected one authorized Android device; found ${#connected_devices[@]}" >&2
        echo "Set ANDROID_SERIAL when more than one device is connected." >&2
        exit 1
    fi
    export ANDROID_SERIAL=${connected_devices[0]}
fi

if [[ "$(adb get-state)" != "device" ]]; then
    echo "The selected Android device is not ready" >&2
    exit 1
fi

abi_list=$(adb shell getprop ro.product.cpu.abilist | tr -d '\r')
sdk_level=$(adb shell getprop ro.build.version.sdk | tr -d '\r')
device_manufacturer=$(adb shell getprop ro.product.manufacturer | tr -d '\r')
device_model=$(adb shell getprop ro.product.model | tr -d '\r')
vulkan_feature=$(adb shell pm list features \
    | tr -d '\r' \
    | grep -E '^feature:android\.hardware\.vulkan\.version=' \
    | head -n 1 \
    || true)

if [[ ",$abi_list," != *",arm64-v8a,"* ]]; then
    echo "The selected device does not report arm64-v8a: $abi_list" >&2
    exit 1
fi
if [[ ! "$sdk_level" =~ ^[0-9]+$ || "$sdk_level" -lt 30 ]]; then
    echo "Android API 30 or newer is required; found: $sdk_level" >&2
    exit 1
fi
if [[ -z "$vulkan_feature" ]]; then
    echo "The selected device does not report the required Vulkan feature" >&2
    exit 1
fi

smoke_duration=${SMOKE_DURATION_SECONDS:-10}
if [[ ! "$smoke_duration" =~ ^[1-9][0-9]*$ || "$smoke_duration" -gt 300 ]]; then
    echo "SMOKE_DURATION_SECONDS must be between 1 and 300" >&2
    exit 1
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/../.." && pwd)
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
output_dir=${2:-"$repo_root/artifacts/android-smoke/$timestamp"}
mkdir -p "$output_dir"

EXPECTED_APPLICATION_ID="$PACKAGE_ID" \
    "$script_dir/verify-apk.sh" "$apk_path"

apk_sha256=$(sha256sum "$apk_path" | awk '{ print $1 }')
commit=$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || echo unknown)

{
    echo "timestamp_utc=$timestamp"
    echo "commit=$commit"
    echo "apk_sha256=$apk_sha256"
    echo "package_id=$PACKAGE_ID"
    echo "device=$device_manufacturer $device_model"
    echo "android_api=$sdk_level"
    echo "abi_list=$abi_list"
    echo "vulkan_feature=$vulkan_feature"
    echo "smoke_duration_seconds=$smoke_duration"
} > "$output_dir/report.txt"

adb install -r "$apk_path" | tee "$output_dir/install.txt"
adb shell am force-stop "$PACKAGE_ID"

launch_output=$(adb shell am start -W -n "$MAIN_COMPONENT" | tr -d '\r')
printf '%s\n' "$launch_output" | tee "$output_dir/launch.txt"
if ! grep -q '^Status: ok$' "$output_dir/launch.txt"; then
    echo "Android did not report a successful activity launch" >&2
    exit 1
fi

app_pid=""
for _ in $(seq 1 20); do
    app_pid=$(adb shell pidof "$PACKAGE_ID" 2>/dev/null | tr -d '\r' || true)
    [[ -n "$app_pid" ]] && break
    sleep 0.25
done
if [[ -z "$app_pid" ]]; then
    echo "The application process was not alive after launch" >&2
    exit 1
fi

logcat_process=""
cleanup() {
    if [[ -n "$logcat_process" ]]; then
        kill "$logcat_process" 2>/dev/null || true
        wait "$logcat_process" 2>/dev/null || true
    fi
    adb shell am force-stop "$PACKAGE_ID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

adb logcat --pid="${app_pid%% *}" -T 1 -v threadtime \
    > "$output_dir/logcat.txt" 2>&1 &
logcat_process=$!
sleep "$smoke_duration"

running_pid=$(adb shell pidof "$PACKAGE_ID" 2>/dev/null | tr -d '\r' || true)
if [[ -z "$running_pid" ]]; then
    echo "The application process exited during the smoke interval" >&2
    exit 1
fi

kill "$logcat_process" 2>/dev/null || true
wait "$logcat_process" 2>/dev/null || true
logcat_process=""

if grep -Eiq 'FATAL EXCEPTION|Fatal signal|ANR in io\.github\.rickamaral94\.cemu' \
    "$output_dir/logcat.txt"; then
    echo "A fatal error or ANR was detected in the application log" >&2
    exit 1
fi

package_version=$(adb shell dumpsys package "$PACKAGE_ID" \
    | tr -d '\r' \
    | grep -E 'versionCode=|versionName=' \
    | head -n 2 \
    || true)
printf '%s\n' "$package_version" >> "$output_dir/report.txt"
echo "result=passed" >> "$output_dir/report.txt"

trap - EXIT
cleanup

echo "Physical-device smoke test passed. Evidence: $output_dir"
