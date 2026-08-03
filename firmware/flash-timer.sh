#!/bin/bash
# flash-timer.sh — flash a USB-attached F3K timer over the cable, from Linux.
#
#     sudo ./flash-timer.sh                    # binaries next to this script, or ./
#     sudo ./flash-timer.sh fw-v37             # releases/fw-v37/
#     sudo ./flash-timer.sh /path/to/build     # any directory with the three .bin files
#     sudo ./flash-timer.sh --port /dev/ttyUSB0 fw-v37
#     sudo ./flash-timer.sh --app-only fw-v37  # skip bootloader + partition table
#
# ── When you need this ───────────────────────────────────────────────────────
#
# Firmware is normally base-managed: the base station sweeps every 20 s and
# updates any connected timer running an older build, with no buttons pressed.
# That path cannot reach two kinds of timer, and both need a cable:
#
#   1. A build older than fw-v37, which predates the OTAPUSH command and simply
#      ignores it. You cannot push to a timer that cannot hear the push.
#   2. A timer that never associates with the base station at all.
#
# ── Four traps, all of which fail silently or look like a different fault ────
#
#   1. THE OTA SLOT TRAP — the big one. The partition table has two app slots,
#      ota_0 at 0x10000 and ota_1 at 0x310000, with otadata at 0xe000 choosing
#      which one boots. A timer that has ever taken an over-the-air update is
#      running from ota_1, so writing 0x10000 fills the slot that is NOT running.
#      esptool prints "Hash of data verified", the timer reboots, and comes up on
#      the OLD firmware. This script always clears otadata afterwards, forcing the
#      fallback to ota_0 — the slot it just wrote.
#   2. Some esptool builds ship without the ESP32-S3 stub flasher (Raspberry Pi
#      OS is one), so --no-stub is used throughout. It is slower and always works.
#   3. --no-stub has no erase_region, so otadata is cleared by writing 8 KB of
#      0xFF over it. write_flash erases its sectors first, so it comes to the same
#      thing.
#   4. On a base station, f3k-timer-serial.service owns the port. It is stopped
#      first and started again at the end — and CHECKED, because it does not
#      reliably survive the device reset, and a dead logger looks exactly like
#      dead firmware: no serial output at all from a perfectly healthy timer.
#
# ⚠ NEVER trust the flash succeeding. "Hash of data verified" proves only that
#   bytes reached the chip, not which slot boots. Confirm the version on the
#   timer's own screen, or from its JOIN line at the base station.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT=""
APP_ONLY=no
SRC=""

usage() { sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --port)     PORT="${2:-}"; shift 2 ;;
        --app-only) APP_ONLY=yes; shift ;;
        -h|--help)  usage 0 ;;
        -*)         echo "unknown option: $1" >&2; usage 1 ;;
        *)          SRC="$1"; shift ;;
    esac
done

die() { echo "ERROR: $*" >&2; exit 1; }
say() { echo "==> $*"; }

[ "$(id -u)" -eq 0 ] || die "run with sudo — flashing needs the serial port"

# esptool is 'esptool' on Debian/Pi OS and 'esptool.py' from pip.
ESPTOOL=""
for c in esptool esptool.py; do command -v "$c" >/dev/null 2>&1 && { ESPTOOL="$c"; break; }; done
[ -n "$ESPTOOL" ] || die "esptool not found. Debian/Pi OS: sudo apt install esptool"

# ── Find the port ────────────────────────────────────────────────────────────
if [ -z "$PORT" ]; then
    for p in /dev/ttyACM0 /dev/ttyACM1 /dev/ttyUSB0 /dev/ttyUSB1; do
        [ -e "$p" ] && { PORT="$p"; break; }
    done
fi
[ -n "$PORT" ] || die "no serial port found — plug the timer in, or pass --port"
[ -e "$PORT" ] || die "$PORT does not exist"

# ── Find the binaries ────────────────────────────────────────────────────────
# A bare name like "fw-v37" means releases/fw-v37 next to this script, so a
# tester who unpacked a release can name the version and nothing else.
if [ -n "$SRC" ]; then
    if   [ -d "$SRC" ];                     then DIR="$SRC"
    elif [ -d "$HERE/releases/$SRC" ];      then DIR="$HERE/releases/$SRC"
    elif [ -d "$HERE/$SRC" ];               then DIR="$HERE/$SRC"
    else die "cannot find '$SRC' — not a directory, and no releases/$SRC"
    fi
else
    # No argument: the binaries beside this script, else the current directory.
    if   [ -f "$HERE/firmware.bin" ]; then DIR="$HERE"
    elif [ -f "./firmware.bin" ];     then DIR="."
    else die "no firmware.bin here. Pass a release name (e.g. fw-v37) or a directory."
    fi
fi

[ -f "$DIR/firmware.bin" ] || die "$DIR/firmware.bin missing"
WRITE_ARGS=(0x10000 "$DIR/firmware.bin")
MODE="app-only"

if [ "$APP_ONLY" = no ]; then
    if [ -f "$DIR/bootloader.bin" ] && [ -f "$DIR/partitions.bin" ]; then
        MODE="full"
        WRITE_ARGS=(0x0 "$DIR/bootloader.bin" 0x8000 "$DIR/partitions.bin" "${WRITE_ARGS[@]}")
    else
        say "no bootloader/partitions here — writing the app only"
    fi
fi

VER=""
[ -f "$DIR/release.txt" ] && VER=$(grep -oE 'fw-v[0-9]+' "$DIR/release.txt" | head -1)
[ -z "$VER" ] && VER=$(basename "$DIR")

say "port   : $PORT"
say "source : $DIR"
say "mode   : $MODE"
say "image  : $(stat -c %s "$DIR/firmware.bin" 2>/dev/null || echo '?') bytes"

# ── Give the serial logger's port back, if there is one ──────────────────────
LOGGER="f3k-timer-serial.service"
LOGGER_WAS_UP=no
if command -v systemctl >/dev/null 2>&1 && systemctl is-active --quiet "$LOGGER" 2>/dev/null; then
    LOGGER_WAS_UP=yes
    say "stopping $LOGGER (it owns $PORT)"
    systemctl stop "$LOGGER"
fi

restore_logger() {
    [ "$LOGGER_WAS_UP" = yes ] || return 0
    systemctl start "$LOGGER" 2>/dev/null
    sleep 2
    if systemctl is-active --quiet "$LOGGER"; then
        say "$LOGGER back up"
    else
        echo "WARNING: $LOGGER did NOT restart. Serial logging is dead until you" >&2
        echo "         start it — and that looks exactly like dead firmware." >&2
    fi
}
trap restore_logger EXIT

# ── Flash ────────────────────────────────────────────────────────────────────
say "writing firmware (this takes ~30s with --no-stub)"
"$ESPTOOL" --chip esp32s3 --port "$PORT" --no-stub write_flash "${WRITE_ARGS[@]}" \
    || die "flash failed — the timer's boot slot is unchanged, it will still run the old build"

# Trap 1, applied unconditionally. Costs two seconds; skipping it costs an hour
# of believing a verified flash did nothing.
say "clearing otadata so the timer boots the slot we just wrote"
BLANK=$(mktemp)
head -c 8192 /dev/zero | tr '\0' '\377' > "$BLANK"
"$ESPTOOL" --chip esp32s3 --port "$PORT" --no-stub write_flash 0xe000 "$BLANK" \
    || { rm -f "$BLANK"; die "otadata clear FAILED — the timer may still boot the OLD firmware"; }
rm -f "$BLANK"

restore_logger
trap - EXIT

# ── Prove it, if this box is a base station ──────────────────────────────────
echo
if command -v journalctl >/dev/null 2>&1 \
   && systemctl list-units --all 2>/dev/null | grep -q f3k-server; then
    say "waiting for the timer to rejoin the base (up to 90s)…"
    SINCE=$(date '+%Y-%m-%d %H:%M:%S')
    GOT=""
    for _ in $(seq 1 30); do
        sleep 3
        GOT=$(journalctl -u f3k-server --since "$SINCE" --no-pager 2>/dev/null \
              | grep -o 'JOIN .*fw=[^ ]*' | tail -1 | sed 's/.*fw=//')
        [ -n "$GOT" ] && break
    done
    if [ -n "$GOT" ]; then
        echo "OK — timer rejoined on $GOT"
        [ -n "$VER" ] && [ "$GOT" != "$VER" ] && \
            echo "NOTE: expected $VER. Check you flashed the build you meant to."
    else
        echo "No JOIN seen. The flash may still be fine — a timer only joins once it"
        echo "reaches the base station's AP. Check the version on its screen."
    fi
else
    echo "Flashed. ⚠ Confirm the version on the timer's screen before trusting it:"
    echo "  hold R at idle (standalone) and read the firmware line, or watch it JOIN"
    echo "  at the base station. 'Hash of data verified' does not prove which slot boots."
fi
