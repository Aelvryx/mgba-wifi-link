#!/usr/bin/env bash

set -euo pipefail

readonly EXPECTED_RELEASE_COMMIT="${EXPECTED_RELEASE_COMMIT:-c9b181aa24d5f2136a6e11fca56179b5204555be}"
readonly EXPECTED_RELEASE_TAG="${EXPECTED_RELEASE_TAG:-v0.1.0-alpha.2}"
readonly EXPECTED_CORE_SHA256="${EXPECTED_CORE_SHA256:-14978106a3978ab4ef6ec025add82e0e9a38decda72404e3dc8c02b3373f179d}"
readonly EXPECTED_CORE_VERSION="${EXPECTED_CORE_VERSION:-0.11-feature/wifi-link-netplay-v2-9146-c9b181aa2}"
readonly EXPECTED_FRONTEND_VERSION="${EXPECTED_FRONTEND_VERSION:-1.22.2}"
readonly EXPECTED_FRONTEND_GIT="${EXPECTED_FRONTEND_GIT:-69a4f0e}"
readonly EXPECTED_FRONTEND_PACKAGE_VERSION="${EXPECTED_FRONTEND_PACKAGE_VERSION:-1.22.2_GIT}"
readonly EXPECTED_CONTENT_CRC32="${EXPECTED_CONTENT_CRC32:-0x8e91cd13}"
readonly EXPECTED_ROM_SHA256="${EXPECTED_ROM_SHA256:-aa0cc276932c99f27f16f8d9a1d8ca08f9729e1b986848286200e3dc7b8b025e}"
readonly PACKAGE="${PACKAGE:-com.retroarch.aarch64}"
readonly ACTIVITY="${ACTIVITY:-${PACKAGE}/com.retroarch.browser.retroactivity.RetroActivityFuture}"
readonly INTERNAL_CORE="${INTERNAL_CORE:-/data/user/0/${PACKAGE}/cores/mgba_libretro_android.so}"
readonly CONTENT="${CONTENT:-/sdcard/Roms/gba/The Legend of Zelda - A Link to the Past & Four Swords.zip}"
readonly THOR_SERIAL="${THOR_SERIAL:-11c5b80}"
readonly ODIN_SERIAL="${ODIN_SERIAL:-6986c674}"
readonly THOR_CONTROLLER="${THOR_CONTROLLER:-Ayn Odin}"
readonly ODIN_CONTROLLER="${ODIN_CONTROLLER:-Ayn Odin (Xbox Mode)}"
readonly EXPECTED_LATENCY_POLICY="${EXPECTED_LATENCY_POLICY:-stable}"
readonly EXPECTED_SELECTED_DELAY="${EXPECTED_SELECTED_DELAY:-2}"

if [[ -n "${ADB:-}" ]]; then
  readonly ADB_BIN="$ADB"
elif command -v adb >/dev/null 2>&1; then
  readonly ADB_BIN="$(command -v adb)"
else
  readonly ADB_BIN="/var/home/anthony/Android/Sdk/platform-tools/adb"
fi

readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly QUALIFICATION_BASE="${QUALIFICATION_BASE:-${REPO_ROOT}/.qualification/four-swords-discovery}"
readonly REMOTE_BASE="${REMOTE_BASE:-/sdcard/Android/data/${PACKAGE}/files/mgba-four-swords-discovery}"
readonly CORE_PATH="${CORE_PATH:-${REPO_ROOT}/build-android-v2-reviewed-arm64/mgba_libretro.so}"
readonly VALIDATOR="${VALIDATOR:-${REPO_ROOT}/tools/four-swords-discovery/qualification-validate.py}"

usage() {
  cat <<'EOF'
Usage: RUN_ID=<id> android-qualification.sh COMMAND

Commands: preflight, stage, launch, check-controls, capture, stop, cleanup

The ignored <run>/manifest.json file is mandatory. The exact core must first
be installed through RetroArch's human-owned core-installation flow; Android
does not permit ADB to hash the resulting app-private file.

Never use `adb shell input` during this workflow. Android's synthetic Virtual
controller can claim RetroArch port 1 and displace the handheld controls.
EOF
}

require_run_id() {
  if [[ -z "${RUN_ID:-}" ||
        ! "$RUN_ID" =~ ^[A-Za-z0-9]([A-Za-z0-9._-]{0,62}[A-Za-z0-9])?$ ]]; then
    echo "RUN_ID must be a 1-64 character normal path component beginning and ending with a letter or digit" >&2
    exit 2
  fi
}

local_root() {
  printf '%s/%s' "$QUALIFICATION_BASE" "$RUN_ID"
}

remote_root() {
  printf '%s/%s' "$REMOTE_BASE" "$RUN_ID"
}

manifest_path() {
  printf '%s' "${MANIFEST_PATH:-$(local_root)/manifest.json}"
}

assert_path_contract() {
  local local_path
  local remote_path
  local_path="$(local_root)"
  remote_path="$(remote_root)"
  if [[ "$local_path" != "$QUALIFICATION_BASE/"* || "$local_path" == "$QUALIFICATION_BASE/" ]]; then
    echo "Refusing qualification path outside the local base: $local_path" >&2
    exit 2
  fi
  if [[ "$remote_path" != "$REMOTE_BASE/"* || "$remote_path" == "$REMOTE_BASE/" ]]; then
    echo "Refusing qualification path outside the remote base: $remote_path" >&2
    exit 2
  fi
}

for_each_device() {
  local callback=$1
  "$callback" "$THOR_SERIAL" thor "$THOR_CONTROLLER" 0
  "$callback" "$ODIN_SERIAL" odin "$ODIN_CONTROLLER" 1
}

assert_device() {
  local serial=$1
  local name=$2
  local state
  state="$($ADB_BIN -s "$serial" get-state 2>/dev/null || true)"
  [[ "$state" == device ]] || { echo "$name ($serial) is not connected over ADB" >&2; exit 1; }
}

assert_frontend_stopped() {
  local serial=$1
  local name=$2
  if [[ -n "$($ADB_BIN -s "$serial" shell "pidof $PACKAGE || true" | tr -d '\r')" ]]; then
    echo "$name has RetroArch running; stop it before staging or preflight" >&2
    exit 1
  fi
}

assert_remote_absent() {
  local serial=$1
  local name=$2
  local remote
  remote="$(remote_root)"
  assert_path_contract
  if $ADB_BIN -s "$serial" shell "test -e '$remote'"; then
    echo "$name already has qualification run $RUN_ID; capture/cleanup it or choose a fresh RUN_ID" >&2
    exit 1
  fi
}

local_sha256() {
  sha256sum "$1" | awk '{print $1}'
}

remote_sha256() {
  local serial=$1
  local path=$2
  $ADB_BIN -s "$serial" shell "sha256sum '$path'" | tr -d '\r' | awk 'NR == 1 {print $1}'
}

validate_local_inputs() {
  local root
  local manifest
  local remote
  local name
  local cfg
  root="$(local_root)"
  manifest="$(manifest_path)"
  remote="$(remote_root)"
  assert_path_contract
  [[ -x "$VALIDATOR" || -f "$VALIDATOR" ]] || { echo "Missing validator: $VALIDATOR" >&2; exit 1; }
  python3 "$VALIDATOR" manifest \
    --manifest "$manifest" --run-id "$RUN_ID" --core "$CORE_PATH" \
    --core-sha256 "$EXPECTED_CORE_SHA256" --core-version "$EXPECTED_CORE_VERSION" \
    --release-commit "$EXPECTED_RELEASE_COMMIT" --release-tag "$EXPECTED_RELEASE_TAG" \
    --frontend-version "$EXPECTED_FRONTEND_VERSION" --frontend-git "$EXPECTED_FRONTEND_GIT" \
    --frontend-package-version "$EXPECTED_FRONTEND_PACKAGE_VERSION" \
    --package "$PACKAGE" --content-crc32 "$EXPECTED_CONTENT_CRC32" \
    --rom-sha256 "$EXPECTED_ROM_SHA256" \
    --thor-serial "$THOR_SERIAL" --odin-serial "$ODIN_SERIAL" \
    --thor-controller "$THOR_CONTROLLER" --odin-controller "$ODIN_CONTROLLER" \
    --latency-policy "$EXPECTED_LATENCY_POLICY" \
    --selected-delay "$EXPECTED_SELECTED_DELAY"
  for name in thor odin; do
    cfg="$root/device-snapshots/$name-qualification.cfg"
    python3 "$VALIDATOR" config --config "$cfg" \
      --options "$root/device-snapshots/$name-mgba-qualification.opt" \
      --remote-root "$remote" --latency-policy "$EXPECTED_LATENCY_POLICY"
  done
}

preflight_device() {
  local serial=$1
  local name=$2
  local version_line
  assert_device "$serial" "$name"
  assert_frontend_stopped "$serial" "$name"
  assert_remote_absent "$serial" "$name"
  version_line="$($ADB_BIN -s "$serial" shell "dumpsys package $PACKAGE | grep -m1 versionName=" | tr -d '\r')"
  version_line=${version_line#*versionName=}
  version_line=${version_line%%[[:space:]]*}
  [[ "$version_line" == "$EXPECTED_FRONTEND_PACKAGE_VERSION" ]] || {
    echo "$name RetroArch version mismatch: $version_line" >&2
    exit 1
  }
  printf '%s: ' "$name"
  $ADB_BIN -s "$serial" shell 'getprop ro.product.model; getprop ro.build.version.release' | tr '\n' ' ' | tr -d '\r'
  printf 'versionName=%s\n' "$version_line"
}

stage_device() {
  local serial=$1
  local name=$2
  local root
  local remote
  root="$(local_root)"
  remote="$(remote_root)"
  assert_path_contract
  $ADB_BIN -s "$serial" shell "mkdir -p '$remote/core' '$remote/config' '$remote/logs' '$remote/saves/mGBA' '$remote/states/mGBA'"
  $ADB_BIN -s "$serial" push "$CORE_PATH" "$remote/core/mgba_libretro_android.so" >/dev/null
  $ADB_BIN -s "$serial" push "$root/device-snapshots/$name-qualification.cfg" "$remote/config/retroarch-qualification.cfg" >/dev/null
  $ADB_BIN -s "$serial" push "$root/device-snapshots/$name-mgba-qualification.opt" "$remote/config/mgba-qualification.opt" >/dev/null
  $ADB_BIN -s "$serial" push "$root/saves/$name/qualification-pre-run.srm" "$remote/saves/mGBA/The Legend of Zelda - A Link to the Past & Four Swords.srm" >/dev/null
}

assert_remote_staging() {
  local serial=$1
  local name=$2
  local remote
  local root
  local pairs
  local local_file
  local remote_file
  local expected
  local actual
  root="$(local_root)"
  remote="$(remote_root)"
  assert_path_contract
  if ! $ADB_BIN -s "$serial" shell "test -d '$remote/core' -a -d '$remote/config' -a -d '$remote/logs' -a -d '$remote/saves/mGBA' -a -d '$remote/states/mGBA'"; then
    echo "$name staged qualification directory is incomplete" >&2
    exit 1
  fi
  pairs=(
    "$CORE_PATH|$remote/core/mgba_libretro_android.so"
    "$root/device-snapshots/$name-qualification.cfg|$remote/config/retroarch-qualification.cfg"
    "$root/device-snapshots/$name-mgba-qualification.opt|$remote/config/mgba-qualification.opt"
    "$root/saves/$name/qualification-pre-run.srm|$remote/saves/mGBA/The Legend of Zelda - A Link to the Past & Four Swords.srm"
  )
  for pair in "${pairs[@]}"; do
    local_file=${pair%%|*}
    remote_file=${pair#*|}
    expected="$(local_sha256 "$local_file")"
    actual="$(remote_sha256 "$serial" "$remote_file")"
    if [[ "$actual" != "$expected" ]]; then
      echo "$name remote hash mismatch for $remote_file: expected $expected, got ${actual:-missing}" >&2
      exit 1
    fi
  done
}

launch_device() {
  local serial=$1
  local name=$2
  local remote
  remote="$(remote_root)"
  assert_device "$serial" "$name"
  assert_path_contract
  $ADB_BIN -s "$serial" shell am force-stop "$PACKAGE"
  $ADB_BIN -s "$serial" shell "am start -W -n '$ACTIVITY' --es CONFIGFILE '$remote/config/retroarch-qualification.cfg' --es LIBRETRO '$INTERNAL_CORE' --es ROM '$CONTENT'" | tr -d '\r'
}

latest_log() {
  local serial=$1
  local remote
  remote="$(remote_root)"
  assert_path_contract
  $ADB_BIN -s "$serial" shell "ls -1t '$remote/logs/'*.log 2>/dev/null | head -1" | tr -d '\r'
}

check_control_device() {
  local serial=$1
  local name=$2
  local expected_controller=$3
  local expected_role=$4
  local log
  local remote
  assert_device "$serial" "$name"
  assert_remote_staging "$serial" "$name"
  remote="$(remote_root)"
  log="$(latest_log "$serial")"
  [[ -n "$log" ]] || { echo "$name has no qualification log" >&2; exit 1; }
  echo "===== $name"
  $ADB_BIN -s "$serial" shell "grep -E '\[Autoconf\]|Found joypad|registered replicated-pair|CRC32|Loading dynamic libretro core|^RetroArch |attach P[01] policy=|calibration P[01]|cal-(rtt|select|digest-[ab]) P[01]' '$log'" | tr -d '\r'
  if ! $ADB_BIN -s "$serial" shell "cat '$log'" | tr -d '\r' | \
      python3 "$VALIDATOR" runtime-log \
        --frontend-version "$EXPECTED_FRONTEND_VERSION" \
        --frontend-git "$EXPECTED_FRONTEND_GIT" \
        --internal-core "$INTERNAL_CORE" \
        --content-crc32 "$EXPECTED_CONTENT_CRC32" \
        --remote-root "$remote" \
        --expected-controller "$expected_controller" \
        --expected-role "$expected_role" \
        --latency-policy "$EXPECTED_LATENCY_POLICY" \
        --selected-delay "$EXPECTED_SELECTED_DELAY"; then
    echo "$name failed prepared-run validation; stop and relaunch without ADB input injection" >&2
    exit 1
  fi
  echo "$name prepared-run contract passed"
}

capture_device() {
  local serial=$1
  local name=$2
  local root
  local remote
  root="$(local_root)"
  remote="$(remote_root)"
  assert_path_contract
  mkdir -p "$root/logs/$name/post-run" "$root/saves/$name/post-run"
  $ADB_BIN -s "$serial" pull "$remote/logs" "$root/logs/$name/post-run" >/dev/null
  $ADB_BIN -s "$serial" pull "$remote/saves/mGBA/The Legend of Zelda - A Link to the Past & Four Swords.srm" "$root/saves/$name/post-run/four-swords.srm" >/dev/null
}

stop_device() {
  local serial=$1
  local name=$2
  assert_device "$serial" "$name"
  $ADB_BIN -s "$serial" shell am force-stop "$PACKAGE"
  echo "$name stopped"
}

cleanup_device() {
  local serial=$1
  local name=$2
  local remote
  remote="$(remote_root)"
  assert_path_contract
  assert_device "$serial" "$name"
  assert_frontend_stopped "$serial" "$name"
  $ADB_BIN -s "$serial" shell "rm -rf '$remote'"
  if $ADB_BIN -s "$serial" shell "test -e '$remote'"; then
    echo "$name qualification directory still exists: $remote" >&2
    exit 1
  fi
  echo "$name qualification directory removed"
}

main() {
  local command=${1:-}
  require_run_id
  assert_path_contract
  case "$command" in
    preflight)
      validate_local_inputs
      for_each_device preflight_device
      ;;
    stage)
      validate_local_inputs
      for_each_device assert_device
      for_each_device assert_frontend_stopped
      for_each_device assert_remote_absent
      for_each_device stage_device
      for_each_device assert_remote_staging
      ;;
    launch)
      validate_local_inputs
      for_each_device assert_remote_staging
      for_each_device launch_device
      echo "Do not inject ADB input. Press one physical button on each device, then run check-controls."
      ;;
    check-controls)
      validate_local_inputs
      for_each_device check_control_device
      ;;
    capture)
      for_each_device capture_device
      ;;
    stop)
      for_each_device stop_device
      ;;
    cleanup)
      for_each_device cleanup_device
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  main "$@"
fi
