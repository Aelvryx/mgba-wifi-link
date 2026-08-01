#!/usr/bin/env bash

set -euo pipefail

readonly EXPECTED_CORE_SHA256="14978106a3978ab4ef6ec025add82e0e9a38decda72404e3dc8c02b3373f179d"
readonly PACKAGE="com.retroarch.aarch64"
readonly ACTIVITY="${PACKAGE}/com.retroarch.browser.retroactivity.RetroActivityFuture"
readonly INTERNAL_CORE="/data/user/0/${PACKAGE}/cores/mgba_libretro_android.so"
readonly CONTENT="/sdcard/Roms/gba/The Legend of Zelda - A Link to the Past & Four Swords.zip"
readonly THOR_SERIAL="${THOR_SERIAL:-11c5b80}"
readonly ODIN_SERIAL="${ODIN_SERIAL:-6986c674}"

if [[ -n "${ADB:-}" ]]; then
  readonly ADB_BIN="$ADB"
elif command -v adb >/dev/null 2>&1; then
  readonly ADB_BIN="$(command -v adb)"
else
  readonly ADB_BIN="/var/home/anthony/Android/Sdk/platform-tools/adb"
fi

readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly CORE_PATH="${CORE_PATH:-${REPO_ROOT}/build-android-v2-reviewed-arm64/mgba_libretro.so}"

usage() {
  cat <<'EOF'
Usage: RUN_ID=<id> android-qualification.sh COMMAND

Commands: preflight, stage, launch, check-controls, capture, stop, cleanup

Never use `adb shell input` during this workflow. Android's synthetic Virtual
controller can claim RetroArch port 1 and displace the handheld controls.
EOF
}

require_run_id() {
  if [[ -z "${RUN_ID:-}" || ! "$RUN_ID" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "RUN_ID must contain only letters, digits, dots, underscores, or hyphens" >&2
    exit 2
  fi
}

local_root() {
  printf '%s/.qualification/four-swords-discovery/%s' "$REPO_ROOT" "$RUN_ID"
}

remote_root() {
  printf '/sdcard/Android/data/%s/files/mgba-four-swords-discovery/%s' "$PACKAGE" "$RUN_ID"
}

for_each_device() {
  local callback=$1
  "$callback" "$THOR_SERIAL" thor
  "$callback" "$ODIN_SERIAL" odin
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

validate_local_inputs() {
  local root
  local name
  local cfg
  local save
  root="$(local_root)"
  [[ -f "$CORE_PATH" ]] || { echo "Missing core: $CORE_PATH" >&2; exit 1; }
  echo "$EXPECTED_CORE_SHA256  $CORE_PATH" | sha256sum --check --status || {
    echo "Core hash does not match alpha.2" >&2
    exit 1
  }
  for name in thor odin; do
    cfg="$root/device-snapshots/$name-qualification.cfg"
    save="$root/saves/$name/qualification-pre-run.srm"
    [[ -f "$cfg" ]] || { echo "Missing qualification config: $cfg" >&2; exit 1; }
    [[ -f "$save" ]] || { echo "Missing isolated save: $save" >&2; exit 1; }
    grep -qx 'input_overlay_enable = "true"' "$cfg" || {
      echo "$name must visibly enable the touchscreen overlay" >&2
      exit 1
    }
    grep -qx 'input_player1_joypad_index = "0"' "$cfg" || {
      echo "$name must retain native joypad index 0" >&2
      exit 1
    }
    grep -qx 'input_netplay_host_toggle = "nul"' "$cfg" || {
      echo "$name must not use an injected host hotkey" >&2
      exit 1
    }
  done
}

preflight_device() {
  local serial=$1
  local name=$2
  assert_device "$serial" "$name"
  assert_frontend_stopped "$serial" "$name"
  printf '%s: ' "$name"
  $ADB_BIN -s "$serial" shell 'getprop ro.product.model; getprop ro.build.version.release' | tr '\n' ' ' | tr -d '\r'
  $ADB_BIN -s "$serial" shell "dumpsys package $PACKAGE | grep -m1 versionName=" | tr -d '\r'
}

stage_device() {
  local serial=$1
  local name=$2
  local root
  local remote
  root="$(local_root)"
  remote="$(remote_root)"
  $ADB_BIN -s "$serial" shell "mkdir -p '$remote/core' '$remote/config' '$remote/logs' '$remote/saves/mGBA' '$remote/states/mGBA'"
  $ADB_BIN -s "$serial" push "$CORE_PATH" "$remote/core/mgba_libretro_android.so" >/dev/null
  $ADB_BIN -s "$serial" push "$root/device-snapshots/$name-qualification.cfg" "$remote/config/retroarch-qualification.cfg" >/dev/null
  $ADB_BIN -s "$serial" push "$root/saves/$name/qualification-pre-run.srm" "$remote/saves/mGBA/The Legend of Zelda - A Link to the Past & Four Swords.srm" >/dev/null
  $ADB_BIN -s "$serial" shell "sha256sum '$remote/core/mgba_libretro_android.so' '$remote/config/retroarch-qualification.cfg' '$remote/saves/mGBA/The Legend of Zelda - A Link to the Past & Four Swords.srm'" | tr -d '\r'
}

launch_device() {
  local serial=$1
  local name=$2
  local remote
  remote="$(remote_root)"
  assert_device "$serial" "$name"
  $ADB_BIN -s "$serial" shell am force-stop "$PACKAGE"
  $ADB_BIN -s "$serial" shell "am start -W -n '$ACTIVITY' --es CONFIGFILE '$remote/config/retroarch-qualification.cfg' --es LIBRETRO '$INTERNAL_CORE' --es ROM '$CONTENT'" | tr -d '\r'
}

latest_log() {
  local serial=$1
  local remote
  remote="$(remote_root)"
  $ADB_BIN -s "$serial" shell "ls -1t '$remote/logs/'*.log 2>/dev/null | head -1" | tr -d '\r'
}

check_control_device() {
  local serial=$1
  local name=$2
  local log
  assert_device "$serial" "$name"
  log="$(latest_log "$serial")"
  [[ -n "$log" ]] || { echo "$name has no qualification log" >&2; exit 1; }
  echo "===== $name"
  $ADB_BIN -s "$serial" shell "grep -E '\[Autoconf\]|Found joypad|registered replicated-pair|CRC32' '$log'" | tr -d '\r'
  if ! $ADB_BIN -s "$serial" shell "grep -Eq '\[Autoconf\].*configured in port 1' '$log'"; then
    echo "$name has not proven its real controller on RetroArch port 1." >&2
    echo "Press one physical button on $name, then rerun check-controls." >&2
    exit 1
  fi
}

capture_device() {
  local serial=$1
  local name=$2
  local root
  local remote
  root="$(local_root)"
  remote="$(remote_root)"
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
  case "$command" in
    preflight)
      validate_local_inputs
      for_each_device preflight_device
      ;;
    stage)
      validate_local_inputs
      for_each_device assert_device
      for_each_device assert_frontend_stopped
      for_each_device stage_device
      ;;
    launch)
      validate_local_inputs
      for_each_device launch_device
      echo "Do not inject ADB input. Press one physical button on each device, then run check-controls."
      ;;
    check-controls)
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

main "$@"
