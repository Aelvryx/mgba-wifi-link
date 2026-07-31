#!/bin/sh
set -eu

if [ "$#" -ne 5 ]; then
	echo "usage: $0 ADB SERIAL LABEL SAMPLES INTERVAL_SECONDS" >&2
	exit 2
fi

adb=$1
serial=$2
label=$3
samples=$4
interval=$5

sample=0
while [ "$sample" -lt "$samples" ]; do
	epoch=$(date +%s)
	pid=$($adb -s "$serial" shell pidof com.retroarch.aarch64 | tr -d '\r')
	printf 'sample=%s label=%s epoch=%s pid=%s\n' \
		"$sample" "$label" "$epoch" "${pid:-none}"
	if [ -n "$pid" ]; then
		$adb -s "$serial" shell \
			"grep -E '^(State|VmPeak|VmHWM|VmRSS|Threads|voluntary_ctxt_switches|nonvoluntary_ctxt_switches):' /proc/$pid/status" \
			| tr -d '\r'
		$adb -s "$serial" shell top -b -n 1 -p "$pid" \
			| tail -n 1 | tr -d '\r'
	fi
	thermal_status=$($adb -s "$serial" shell dumpsys thermalservice \
		| sed -n 's/^Thermal Status: /thermal-status=/p' | tr -d '\r')
	printf '%s\n' "${thermal_status:-thermal-status=unknown}"
	$adb -s "$serial" shell \
		'for z in /sys/class/thermal/thermal_zone*; do t=$(cat "$z/type" 2>/dev/null); case "$t" in cpuss-*|cpu-1-*|gpuss-*|battery) v=$(cat "$z/temp" 2>/dev/null); printf "%s=%s " "$t" "$v";; esac; done; echo' \
		| tr -d '\r'
	$adb -s "$serial" shell \
		'for p in /sys/devices/system/cpu/cpufreq/policy*; do n=${p##*/}; c=$(cat "$p/scaling_cur_freq" 2>/dev/null); m=$(cat "$p/cpuinfo_max_freq" 2>/dev/null); printf "%s=%s/%s " "$n" "$c" "$m"; done; echo' \
		| tr -d '\r'
	$adb -s "$serial" shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1 || true
	printf '\n'
	sample=$((sample + 1))
	if [ "$sample" -lt "$samples" ]; then
		sleep "$interval"
	fi
done
