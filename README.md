# F3K Timer

A hand-held competition timer for a **caller** — the pilot's field assistant who coaches the pilot in real time during F3K (discus-launched glider) and F5K rounds.

## Features

- Working time countdown with colour-coded arc (green → orange → red)
- All times displayed with hundredths of a second (MM:SS.CC)
- Flight timer — start/stop with a single button press
- Flight log showing best times with traffic-light ranking:
  - 1st best = Green
  - 2nd best = Orange
  - 3rd best = Yellow
  - Scratched = Red (strikethrough)
- Audio alerts at key time thresholds (30s, 15s, 10–1s countdown)
- Configurable working time (1–15 minutes via settings)
- **F3K / F5K task selection** — choose task type in settings; F5K enables post-round altitude entry
- **Altitude entry (F5K)** — after time expires, enter launch altitude per flight with rollover dials:
  - R = +1 m (ones digit, rolls 0 → 9 → 0)
  - L = +10 m (tens digit, rolls 0 → 10 → … → 100 → 0)
  - R hold = confirm altitude and advance to next flight
- Battery indicator (% + charging state)
- **Base station WiFi connectivity** — connects to F3K_BASE AP, receives TASK/START/STOP/PILOTS/COUNT commands, reports FLIGHT/JUMPED/SCRATCH/ALTITUDE/SELECT back
- **Scratch reaches the base** — a flight is reported the instant it is flown, so scratching it on the timer alone used to leave it valid at the base and in the GliderScore export. `SCRATCH pilot=N dur=M` now goes through the ACK-gated queue; the base flags the row (never deletes it — the end-of-round resend would re-insert a deleted one). ⚠ Requires a base station with the I-42 handler; an older one never ACKs it and the entry retries forever.
- **Jumped starts count as a launch (fw-v31)** — a launch before the window opened is now a *voided flight* at the base, not a note: it takes a flight number and scores zero. It used to be broadcast to the CD and never written down at all, so on every launch-limited task (F3K F allows six, F5K A four, F5K B/D/E three) the pilot kept the launch and could simply throw again for free. FAI 5.7.7: *"a launch before the start of working time scores zero."*
- **Working-time sync (`WTSYNC t=`, fw-v31)** — the base is the master clock and can set the *running* working time in seconds. There was no way to say that before: `TASK wt=` is whole minutes and is only read when a round starts, and `START` is ignored by an already-running timer — so a timer reconnecting mid-working with 8:30 left was told 8:00, and one with 45 s left was told **zero**. `WorkingTime::syncRemaining()` moves the clock without touching the total, the running flight or the local log, and re-bases the alert bookkeeping so a jump from 10:00 to 0:15 does not fire the 2-minute, 1-minute and 30-second calls all at once.
- **Task modes (`TASK … task= mode=`, fw-v31)** — the base now says *which* task and how to fly it (`plain` / `poker` / `ladder`, with the rule's parameters). The timer previously received only working time and discipline, so it could not know it was flying a target task. ⚠ Anything unrecognised — including a base that sends no `mode=` at all — is `plain`: a timer that guessed would demand a target nobody is going to give it.
- **Poker & Ladder target tasks (fw-v31)** — one mechanism, two sources for the target: the pilot's call (Poker) or the current rung (Ladder, +step *when reached*). The flight clock counts **down** to the target, because the timekeeper calls the remaining time to the pilot, and turns orange past it — the call is banked and the rest is wasted airtime. `TGT M:SS` shows before a throw so the pilot knows what they are flying for.
  - **`STATE_TARGET_SET` (reworked fw-v32, after the first hardware press-through)** — L cycles `--- → W → 0 → 1 → … → max → ---`, R steps seconds by 5 **and works on `---` too**, R medium-press (hold and release before 2 s) steps by 1, R-hold 2 s confirms. Both special positions live in the wrap because there is no L-hold to spare — holding L is an AXP2101 power-off that software never sees. `---` is the escape, and `0:00` is refused because it stores as "no target".
  - **`W` comes first, one press from opening.** Every numeric call is worth the same whenever it is entered, but `W` resolves to the working time remaining *at the moment of confirm* — so seconds spent dialling it come straight off the call. It is also the natural quick-turn-around call. It used to be last, up to eleven presses away.
  - **R works on `---`.** The picker opens there so a stray press changes nothing, but R used to be inert until L had been pressed — so the first three presses of a sub-minute call did nothing at all, and it read from outside as "you cannot set under a minute".
  - The 1-second fine adjust is not a nicety: the rulebook's own example call is **2:38**, so a 5-second-only picker could not express a legal target. With every second reachable, judging is strict — achieved iff flown ≥ target, exactly as FAI says. ⚠ It fires **on release**, which feels odd and is deliberate: everything else fires while the button is still down, so an 800 ms action always beat a 2 s one to the punch and the fine adjust was literally unreachable. Do not move it back.
  - **Declarable during prep (fw-v32)** — prep is when the caller has time to think and write the call down. The prep countdown, its beeps and the local window-open fallback all keep running behind the picker, the call survives the window opening, and `W` in prep means the whole window. R stays locked until the last 2 s of prep either way, so this cannot become an accidental launch.
  - **Reachable during a flight.** FAI 2025 F3K.11.5 permits declaring after the launch (*"shown to the timekeeper in written numbers … immediately after the launch"*), which is the quick turn-around: throw first, call while it climbs. The picker shows the running flight clock so the timekeeper can see which throw they are calling against.
  - ⚠ **The countdown lives in `_updateRunningInc()`.** That is the only redraw path that runs while a glider is in the air (every 50 ms); the full-redraw paths run on state *changes*. fw-v31 counted down correctly in every path except that one, so on hardware the clock simply counted up for the whole flight. A render helper not called from the incremental path does not run during a flight.
  - **`W` resolves at the LAUNCH, not at the call (fw-v33).** "The rest of the working time" is measured from when the glider leaves the hand. Resolving it at the call made a `W` unflyable unless the pilot threw in the same second — the clock kept running while they walked to the line and the target never moved, so flying the entire window scored **zero**. While it is armed the screen shows `TGT W m:ss`, ticking down: what the call is worth if thrown now. A `W` called mid-flight resolves immediately, since the launch has already happened.
  - **A `W` is judged on still being airborne when the window shuts**, not by arithmetic. Its target is the time left at launch, so the flight ends within milliseconds of it and a truncating seconds compare could put it one under — failing the one call that can be flown perfectly.
  - **`FLYING (3)`** — the running screen carries the launch number, because on a launch-limited task (F3K F six, F5K A four, F5K B/D/E three) that is what the timekeeper needs. A jumped start is numbered too: it consumes a launch.
  - **No outcome screen.** Landing judges the flight, flashes `ACHIEVED`/`MISSED` and re-arms: a miss keeps the same target (FAI — a call *"cannot be changed"*), a hit clears it in Poker or advances the rung in Ladder. A screen to dismiss is exactly wrong in a turn-around.
  - **`W` ("end of working time") gets ONE attempt** — the single exception to re-flying a missed call. It resolves to a real number of seconds when confirmed, so a concrete time reaches the timekeeper and the export.
  - In Poker, **L declares instead of scratching**: you never move on from a call until it is achieved, so a bad flight is re-flown rather than discarded. Ladder keeps scratch.
  - `FLIGHT … target=<s>` (plus `tw=1` for a `W`) reports the call, which is what makes Poker scoreable — FAI credits the **announced** time, never the flown one.
- **ACK-gated outbound queue (fw-v16)** — every reported message is queued *before* sending, even on a socket that looks healthy, and released only when the base echoes it back as `ACK <line>`. Resent in full on `ASSIGN` (i.e. after any reconnect) and retried after 5s. Sending is not proof of delivery: on a silently dead socket lwIP accepts the write and discards it while `_tcp.connected()` still reports true for up to ~60s, and a flight time written into that hole used to be lost. Needs a base station that ACKs unconditionally — see `docs/PROTOCOL_ACK.md` in the base station repo
- **Firmware version reporting (fw-v17)** — the JOIN handshake carries the running build (`JOIN mac=… fw=…`), so the base station's Settings page can show which timers are still on an old version without picking each one up. Older bases ignore the extra param
- **Screen sleep (fw-v19, AMOLED burn-in)** — blanks to true black after 2 minutes of inactivity, so the pixels are genuinely off. Behaviour depends on whether a USB cable is attached: **in the field** the screen never blanks during a live round, so a caller can never look down mid-flight at a black display; **on the bench** (USB attached, *and standalone*) it blanks regardless of state, since a simulated round can run for minutes with nobody watching. ⚠ **A live round with the base station connected never blanks, cable or not (fw-v34).** The timer is permanently wired to the Pi for development, so bench mode was true for every round anyone actually tested — and the screen blanked mid-flight. A round the base is driving is by definition attended. Any button wakes it, as does anything that moves the timer to a different screen. The base station can force it on for a bounded window with `SCREEN t=N`
- **Pilot selection UI** — scrollable list driven by PILOTS command from base station; SELECT sent on confirm
- **10-second countdown arc** — green sweep during pre-round countdown from base
- **Base-driven prep countdown** — `PREP t=` starts a yellow arc over the full prep time with beeps at 30/15/10–1s; above 10s shows `M:SS.T` with tenths, final 10s shows huge `N.T`; COUNT re-syncs the clock; if START is lost, the round starts locally 250 ms after prep hits 0
- **Start lockout when connected** — idle start buttons disabled (base station owns round starts); flight start unlocks in the final 2s of prep (including the dead zone between local prep clock hitting 0 and `START` arriving)
- **Jumped-start invalidation** — launching before the WT long beep runs the flight flagged JUMPED (red); on stop it is auto-scratched, kept out of history, and reported to the CD via `JUMPED` instead of `FLIGHT`; abort during prep/countdown/landing returns to the results screen if flights were already recorded (preserves F5K altitude entry)
- **Landing window countdown** — `LAND t=` after working time shows an orange arc with big seconds, then the results screen
- **Timer ID display** — after ASSIGN, shows `T1` / `T2` etc. bold green on idle screen between battery indicator and GLIDE title
- **Connection indicator** — idle screen shows BASE… (grey) while connecting, BASE OK (green) when live; BASE OK replaced by pilot name once a pilot is selected
- **NVS round history (ROUND RECALL)** — stores last 3 rounds (discipline, pilot name, flight times, F5K altitudes) to ESP32 NVS; each flight written immediately so data survives power loss mid-round; accessible via `STATE_HISTORY` from the expired screen (L) or settings chain; "N of 3" slot indicator, L=older / R=newer/exit, 8s inactivity timeout
- **Pilot decouple** — timer clears pilot binding automatically when returning to idle after a completed round
- **OTA firmware updates** — settings **page 4** (R-hold ×4 from idle; pages 2 and 3 also self-advance on their timeouts) checks for updates from the base station HTTP server; R hold applies the update; device reboots automatically when done. Verified end to end on hardware in session 64: check → download → flash → self-reboot, confirmed by the version in the next `JOIN`. **No inactivity timeout on this screen — R is the only way out.** It previously auto-exited after 8s, and `OTA_CHECKING` was not treated as busy, so the screen bounced back to IDLE while the version request was still in flight and the check never completed. A timeout is wrong here regardless: fetching and flashing are slow by nature and the user is watching. L re-checks, and a `NO WIFI` result retries by itself once the radio associates.
  - **Only a strictly newer build is offered.** Equal reads `UP TO DATE`; an *older* build on the base station shows orange `BASE IS OLDER` / `UPDATE THE BASE` and no update is offered — the base station is the stale one there, and `startUpdate()` gates on `OTA_AVAILABLE`, so the refusal is enforced rather than merely displayed. ⚠ The comparison is **numeric** (`_fwNum`): `fw-v9` sorts above `fw-v28` as a string, so comparing the strings would be worse than an equality test.
  - ⚠ **Wire-flashing a device that has taken an OTA needs `otadata` cleared first.** It is then running from `ota_1`, so `write_flash 0x10000` writes the slot that is not running: esptool reports `Hash of data verified` and the device boots the **old** firmware. Confirm the running build from the base station's `JOIN … fw=`, never from the flash succeeding.
- **OTA diagnostics** — `[OTA]` serial lines cover every exit path of a check (heap, stack high-water mark, HTTP code, raw payload, parsed version, final status). Added in fw-v27 after a screen that appeared hung turned out to be a 30 ms check plus a render race; without logging the only evidence was the base station's `200 OK`, which proved nothing about what happened next.

## Hardware

**Target device:** Waveshare ESP32-S3-Touch-AMOLED-1.75C (SKU 33691)

| Component | Detail |
|-----------|--------|
| SoC | ESP32-S3R8, dual-core LX7 @ 240 MHz |
| Display | 1.75" AMOLED round, 466×466, CO5300 driver |
| Audio | ES8311 codec via I2S |
| Power management | AXP2101 PMIC (also provides L button via power-key IRQ) |
| Battery | 3.7 V Li-Ion, MX1.25 connector |

### Button layout (stopwatch orientation — buttons at 12 o'clock)

| Physical button | Position | Role |
|----------------|----------|------|
| PWR (AXP2101) | Top-left | **L** — secondary: start WT only, scratch, settings adjust |
| BOOT (GPIO0) | Top-right | **R** — primary: start/stop flight, confirm, navigate |

> Do not hold R (BOOT) while powering on — GPIO0 held LOW at boot forces download mode.

## State Machine

```
IDLE
  R hold       → SETTINGS
  R click      → FLIGHT_RUNNING  (starts WT + flight together; locked while connected to base)
  L click      → WORKING_TIME_RUNNING  (WT only — wait for launch; locked while connected to base)

WORKING_TIME_RUNNING
  R click      → FLIGHT_RUNNING
  L click      → TARGET_SET  (Poker, no target armed — declare the next call)
               → SCRATCH_CONFIRM  (otherwise, if flights recorded)
  R hold 2s   → WORKING_TIME_EXPIRED  (abort)
  WT expires  → WORKING_TIME_EXPIRED

FLIGHT_RUNNING
  R click      → WORKING_TIME_RUNNING  (stop & record; judges the target if armed)
  L click      → TARGET_SET  (Poker — declaring AFTER the launch is permitted)
  R hold 2s   → WORKING_TIME_EXPIRED  (abort, discard flight)
  WT expires  → WORKING_TIME_EXPIRED  (auto-record)

TARGET_SET  (Poker only; shows the running flight clock while you dial)
  L click      → next minute:  --- → 0 → 1 → … → WT max → W → ---
  R click      → +5 s  (wraps 0 → 55)
  R very-long → +1 s  (fine adjust — the rulebook's own example call is 2:38)
  R hold       → confirm, back to whichever screen opened it
               (--- confirms as "no call"; there is no L-hold to spare for a
                cancel — holding L is a hardware power-off)

SCRATCH_CONFIRM
  R click      → WORKING_TIME_RUNNING  (confirmed)
  2s timeout  → WORKING_TIME_RUNNING  (cancelled)

WORKING_TIME_EXPIRED
  R click      → ALTITUDE_ENTRY  (F5K, if flights exist)
               → IDLE  (F3K, clears pilot binding)
  L click      → HISTORY  (browse last round in NVS)

ALTITUDE_ENTRY  (F5K only)
  R click      → +1 m  (ones digit, 0→9→0)
  L click      → +10 m  (tens digit, 0→100→0)
  R hold       → confirm altitude, next flight (or IDLE when done; IDLE clears pilot binding)

HISTORY  (NVS round recall — up to 3 slots)
  L click      → older slot (slot+1, max slot 2)
  R click      → newer slot (slot-1), or IDLE when at slot 0
  R hold       → IDLE
  8s timeout  → IDLE

SETTINGS  (page 1: working time)
  R click      → +1 minute
  L click      → −1 minute
  R hold / 8s timeout → TASK_SELECT

TASK_SELECT  (page 2: task type)
  R or L click → toggle F3K / F5K
  R hold / 3s timeout → OTA_CHECK

OTA_CHECK  (page 4: firmware update — page 3 is ROUND RECALL)
  on entry   → async version check to base station :8080
  R hold     → download + flash update (when available)
  L click    → re-check (recovers NO WIFI / FAILED without leaving the screen)
  offered version older than ours → BASE IS OLDER, no update offered
  R click    → IDLE   (no inactivity timeout — see note)
  auto-retry → a NO WIFI check re-fires by itself once the radio associates
  stays lit while CHECKING / DOWNLOADING / UPDATE AVAILABLE; a pending
  decision must not blank. Terminal states may still sleep (no auto-exit,
  so the panel would otherwise ghost).

PILOT_SELECT  (base station only)
  R click      → next pilot
  L click      → previous pilot
  R hold       → confirm, → IDLE

COUNTDOWN  (base station COUNT 10…1, fallback when no PREP received)
  COUNT N      → display arc + beep
  START        → WORKING_TIME_RUNNING + long tone

PREP  (base station PREP t=N — yellow arc prep countdown)
  local countdown with beeps at 30, 15, 10…1; huge digits in last 10s
  COUNT N      → re-sync local clock to base tick
  R click      → early flight start (only in final 2s; flagged JUMPED — invalid)
  START        → WORKING_TIME_RUNNING or FLIGHT_RUNNING (early flight kept) + long tone
  STOP         → IDLE  (CD abort)
  0 + 250ms, no START → start round locally (packet-loss fallback)

LANDING  (base station LAND t=N after WT end — orange arc)
  R click / 0  → WORKING_TIME_EXPIRED  (results)
  STOP         → IDLE  (CD abort)

SCREEN SLEEP  (overlays every state; AMOLED burn-in)
  no activity for SCREEN_SLEEP_MS → blank to black
    field (no USB): every state EXCEPT a live round
                    (PREP/COUNTDOWN/WT/FLIGHT/SCRATCH/LANDING/TARGET_SET)
    base connected: same as field — a live round NEVER blanks (fw-v34)
    bench (USB) +
      standalone:   every state, including a live round
  any button, or a change of screen → wake + full repaint
  SCREEN t=N from base → forced on for N seconds (0 releases)
  NOTE: the waking press is NOT swallowed — it also acts on the current
        screen, so on IDLE-while-connected (start buttons locked) a click
        is a pure wake, but elsewhere it does its normal job as well
```

## Base Station

The timer connects as a WiFi client to the F3K base station AP (`F3K_BASE`). The base station is a Raspberry Pi running `server.py` on port 8765. See the `F3K_Timer_Project` repo for setup.

Network credentials are hardcoded in `include/config.h` — the timer AP is a closed, dedicated network.

## Building for Hardware

```powershell
# Build and flash
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e waveshare --target upload --upload-port COM4 --project-dir "C:\Kris\Projects\F3K_Timer_1"

# Serial monitor
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --environment waveshare --baud 115200 --project-dir "C:\Kris\Projects\F3K_Timer_1"
```

Flash mode: hold BOOT, tap RESET, release BOOT — device enumerates as USB serial on COM4.

## Firmware Releases

Compiled firmware snapshots are stored in `firmware/releases/` and tagged in git as `fw-vN`.
The last 5 builds are kept on disk; all git tags are kept indefinitely.

### Create a release

```powershell
.\scripts\release-firmware.ps1
```

Builds the waveshare firmware with the correct `FW_VERSION` string embedded, copies binaries
to `firmware/releases/fw-vN/`, updates `firmware/ota/` for base station serving, commits,
tags `fw-vN`, then prints the push command. Run at the end of each session before pushing.

> Do not use `-SkipBuild` for real releases — the embedded version string must match the tag
> or the OTA check will loop.

### Roll back

To revert the device to a previous build, flash the binaries directly from the release folder:

```powershell
esptool.py --chip esp32s3 --port COM4 --baud 921600 write_flash `
    0x00000 firmware\releases\fw-v9\bootloader.bin `
    0x08000 firmware\releases\fw-v9\partitions.bin `
    0x10000 firmware\releases\fw-v9\firmware.bin
```

Flash addresses and the source commit hash are recorded in each `release.txt`.
To inspect or rebuild from an older source state: `git checkout fw-vN`.

## Project Structure

```
include/
  config.h          task types, timing constants, AppState + OtaStatus enums
  fw_version.h      auto-generated by /release-firmware skill; defines FW_VERSION "fw-vN"
  pin_config.h      all GPIO defines
src/
  main.cpp          setup(), loop(), state machine
  timer/            WorkingTime, FlightTimer, FlightLog, RoundHistory (NVS)
  display/          UI (round AMOLED, radial layout), ArcRenderer
  input/            Buttons (AXP2101 PWR key + GPIO0 BOOT)
  audio/            Tones (I2S sine wave alerts)
  ota/              OtaUpdater — async HTTPUpdate + version check (hardware only)
  comms/            TimerComms — WiFi TCP client; ACK-gated pending queue (16 entries)
firmware/
  releases/         last 5 compiled builds (fw-vN/firmware.bin + .bin files)
  ota/              current OTA files served by base station HTTP server
tools/
  serial_log.py     background serial logger — writes rolling 500-line log to serial_log.txt
                    run detached via /serial-monitor skill; Claude reads serial_log.txt live
```

## License

MIT
