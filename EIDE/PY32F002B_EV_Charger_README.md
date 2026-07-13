# PY32F002B EV Charger Firmware

Bare-metal embedded firmware for a programmable EV battery charger built around the **PY32F002AF15P6 / PY32F002B Cortex-M0+ MCU**.

The firmware implements charger sequencing, battery detection, CC/CV charging control, VSET/CSET configuration, SOC estimation, display handling, fan control, AC monitoring, thermal protection, timeout handling, and fault-state management using a deterministic cooperative scheduler and finite-state machine.

> **Safety warning**
>
> This project controls high-voltage and high-power charging hardware. Incorrect firmware, sensing calibration, wiring, or protection thresholds can cause electric shock, fire, battery damage, or hardware failure. Do not test the charger unattended or on a live high-voltage system without appropriate isolation, current limiting, fusing, emergency shutdown, and independent hardware protections.

## Features

- Configurable **VSET** and **CSET** charging targets
- Single-button configuration menu
- TM1637 4-digit display interface
- Battery voltage, current, temperature, and AC-input measurement through ADC
- 60-second battery detection / VSET wake window
- Battery classification based on selected VSET
- Pre-charge and deep-discharge recovery modes
- Controlled soft-start current ramp
- Constant-current / constant-voltage charging state machine
- SOC estimation using a normalized VSET-based lookup model
- Filtered SOC progression while charging
- Charge-complete lock state
- Fan control linked to charging activity
- TL494 enable/disable control
- AC input monitoring and protection handling
- Thermal derating and thermal shutdown logic
- Charging timeout and fault handling
- Cooperative time-triggered task scheduling

## Hardware

| Component | Description |
|---|---|
| MCU | PY32F002AF15P6 / PY32F002B family |
| CPU | ARM Cortex-M0+ |
| Debugger / Programmer | ST-Link V2 |
| Display | TM1637 4-digit 7-segment display |
| Power Controller | TL494-based charger power stage |
| Development Environment | VS Code / EIDE |
| Compiler | Arm GNU Toolchain (`arm-none-eabi-gcc`) |
| Debug / Flash Server | Official Puya OpenOCD |

### Known Display Connections

| Signal | MCU Pin |
|---|---|
| TM1637 DIO | PB1 |
| TM1637 CLK | PB2 |

The TM1637 GPIO lines are configured for open-drain operation.

> Other pin assignments depend on the charger PCB schematic and should be verified against the hardware design before flashing the firmware.

## Firmware Architecture

The firmware is organized as independent modules coordinated by a top-level scheduler.

```text
                 +----------------------+
                 |       main()         |
                 +----------+-----------+
                            |
                    SysTick time base
                            |
          +-----------------+-----------------+
          |                 |                 |
       10 ms             100 ms             1 s
          |                 |                 |
   Button / Menu       ADC Sampling      Display /
      handling       Charger State       Time-based
                         Machine           logic
                            |
             +--------------+--------------+
             |              |              |
          Battery          SOC          Protection
          Model          Tracking         Logic
             |
      Charger State Machine
             |
      +------+------+------+
      |             |      |
     Fan          TL494   LEDs/Display
```

## Main Firmware Modules

### `main`
Responsible for:

- GPIO initialization
- SysTick / timer initialization
- ADC initialization
- charger subsystem initialization
- periodic task scheduling
- AC power event detection
- fan and TL494 output updates
- display update coordination

### `button`
Handles push-button sampling and timing.

The button interface is used for:

- short-press menu navigation
- long-press mode entry and switching
- VSET/CSET selection

### `menu`
Implements the charger configuration interface.

Default settings:

- **VSET:** 67.2 V
- **CSET:** 6.2 A

Supported VSET presets:

```text
54.6 V
54.75 V
58.4 V
58.8 V
67.35 V
69.35 V
71.4 V
73.0 V
83.95 V
84.0 V
87.6 V
```

Supported CSET presets:

```text
6.2 A
7.2 A
8.2 A
9.2 A
10.2 A
```

### `tm1637`
Provides the display driver, including functions for:

- raw segment output
- integer display
- voltage display
- current display

### `adc`
Handles ADC acquisition and measurement conversion for charger feedback signals.

The charger control loop uses measurements such as:

- battery/output voltage
- charging current
- temperature channels
- AC input sensing

### `battery`
Tracks battery-related measurements and battery state relative to the selected VSET.

### `soc`
Calculates and filters the displayed battery state of charge.

SOC behavior includes:

- VSET-normalized voltage thresholds
- controlled upward progression during active charging
- maximum 5% increase per qualification cycle
- no downward SOC movement during an active charging session
- 100% indication only after charge-completion conditions are satisfied

### `charger_sm`
Core charger finite-state machine.

The compiled firmware contains dedicated handlers for:

```text
POWER ON
VSET WAKE
PRECHARGE
DEEP DISCHARGE
SOFT START
CC
CV
CHARGE COMPLETE
STANDBY
FAULT
```

## Charging Sequence

```text
AC ON
  |
  v
POWER ON
  |
  v
VSET WAKE / BATTERY DETECTION
  |
  +--> Battery < 20% VSET ------> STANDBY / BTNG
  |
  +--> 20% to <40% VSET --------> PRECHARGE
  |
  +--> 40% to <65% VSET --------> DEEP DISCHARGE
  |
  +--> >=65% VSET --------------> NORMAL CHARGING
                                      |
                                      v
                                  SOFT START
                                      |
                                      v
                                      CC
                                      |
                                      v
                                      CV
                                      |
                         +------------+------------+
                         |                         |
                   Termination met           Protection fault
                         |                         |
                         v                         v
                 CHARGE COMPLETE                FAULT
```

### Battery Detection

After AC power is applied, the charger enters a **60-second VSET wake / battery detection window**.

Battery classification is relative to the selected VSET:

| Battery Voltage | Mode |
|---|---|
| `< 20% of VSET` | Battery not good / standby |
| `20% to < 40% of VSET` | Pre-charge / battery recovery |
| `40% to < 65% of VSET` | Deep-discharge charging |
| `>= 65% of VSET` | Normal CC/CV charging |

### Soft Start

For a valid battery, charging current is ramped toward the selected CSET instead of being applied immediately.

Target ramp rate:

```text
0.3 A/s
```

### CC/CV Charging

The normal charging sequence consists of:

1. **CC mode** — charging current is regulated toward CSET.
2. **CV mode** — voltage is regulated toward VSET.
3. **Termination** — charging completes when the configured end-of-charge conditions are satisfied.

The VSET wake state and CV charge-termination logic are intentionally separate state-machine phases.

## Protection Logic

The firmware architecture includes support for:

| Protection / State | Purpose |
|---|---|
| SCPT | Short-circuit / excessive-current protection |
| RPPT | Reverse-polarity handling |
| HITP | High-temperature protection |
| OVPT | AC over-voltage protection |
| UNVP | AC under-voltage protection |
| CHTO | Maximum charge-time timeout |
| BTNG | Battery-not-good condition |
| DPDC | Deep-discharge charging indication |
| BTRM | Battery recovery / pre-charge indication |
| 100P | Fully charged indication |

### AC Input Protection Targets

- Over-voltage shutdown: **>= 280 VAC**
- Over-voltage recovery: **<= 270 VAC**
- Start charging: **>= 160 VAC**
- Continue running while input remains **>= 150 VAC**
- Stop below **150 VAC**

### Thermal Protection Targets

- Thermal derating begins at approximately **80 °C**
- Immediate thermal shutdown at **>= 90 °C**
- Automatic thermal recovery at **<= 60 °C**

### Charging Time Limits

- CV safety timeout: **60 minutes** after entering the high-voltage CV region
- Maximum total charging duration: **7 hours**

## Cooperative Scheduler

The firmware uses a deterministic tick-driven scheduler rather than an RTOS.

| Interval | Main Responsibilities |
|---|---|
| 10 ms | Button scanning and menu handling |
| 100 ms | ADC measurement, battery update, SOC update, charger state machine |
| 1 s | Display sequencing and slower time-based logic |

This structure keeps time-critical UI and charger-control operations separated without requiring an RTOS.

## Project Structure

The exact directory tree may vary with the Puya SDK version, but the project follows this general structure:

```text
EIDE/
├── Inc/
│   ├── main.h
│   ├── adc.h
│   ├── battery.h
│   ├── button.h
│   ├── charger_sm.h
│   ├── menu.h
│   ├── soc.h
│   └── tm1637.h
├── Src/
│   ├── main.c
│   ├── adc.c
│   ├── battery.c
│   ├── button.c
│   ├── charger_sm.c
│   ├── menu.c
│   ├── soc.c
│   └── tm1637.c
├── Drivers/
├── Project/
│   ├── Project.elf
│   ├── Project.bin
│   ├── Project.hex
│   └── Project.map
├── startup_py32f002bxx.s
├── py32f002bx5.ld
└── Makefile
```

## Build Requirements

Install:

- Arm GNU Toolchain
- GNU Make
- Official Puya OpenOCD
- ST-Link USB drivers
- VS Code with Cortex-Debug or EIDE, if desired

Verify the compiler:

```bash
arm-none-eabi-gcc --version
```

Verify Make:

```bash
make --version
```

## Building the Firmware

Open a terminal in the project root:

```bash
cd path/to/EIDE
```

Clean previous build files:

```bash
make clean
```

Build:

```bash
make
```

A successful build should generate the firmware output in the `Project/` directory, including `Project.elf`.

## Flashing with ST-Link and Puya OpenOCD

Connect:

```text
ST-Link        Target Board
-------        ------------
SWDIO   -----> SWDIO
SWCLK   -----> SWCLK
NRST    -----> NRST
3.3V    -----> 3.3V
GND     -----> GND
```

Example OpenOCD command:

```bash
openocd \
  -s "<PUYA_OPENOCD_SCRIPTS_PATH>" \
  -f interface/stlink.cfg \
  -f target/py32f002a.cfg \
  -c "program Project/Project.elf verify reset exit"
```

On Windows CMD, the same command can be written as:

```bat
"<OPENOCD_PATH>\openocd.exe" ^
-s "<OPENOCD_PATH>\scripts" ^
-f interface/stlink.cfg ^
-f target/py32f002a.cfg ^
-c "program Project/Project.elf verify reset exit"
```

> Use the official Puya OpenOCD package. Generic OpenOCD builds may detect the Cortex-M0+ core but fail to provide the required Puya flash driver.

## Debugging

The project can be debugged over SWD using:

- ST-Link V2
- `arm-none-eabi-gdb`
- official Puya OpenOCD
- VS Code Cortex-Debug

Typical debugging capabilities:

- halt at `main()`
- breakpoints
- register inspection
- memory inspection
- step-by-step execution

## Important Validation Before Hardware Testing

Before connecting a real battery or enabling the full power stage:

1. Verify all ADC scaling against a calibrated multimeter.
2. Confirm VSET and CSET references with a safe dummy load.
3. Test TL494 enable/disable behavior independently.
4. Verify fan output and thermal sensor polarity.
5. Test AC under-voltage and over-voltage thresholds using an isolated controlled source.
6. Confirm short-circuit protection with current-limited test equipment.
7. Validate every state transition before unattended charging.
8. Keep independent hardware protections active.

## Development Status

Implemented firmware architecture includes:

- [x] PY32F002B project bring-up
- [x] ST-Link programming and SWD debugging
- [x] TM1637 display driver
- [x] Button handling
- [x] VSET/CSET menu logic
- [x] ADC measurement layer
- [x] Battery classification
- [x] Charger finite-state machine
- [x] Pre-charge and deep-discharge states
- [x] Soft-start state
- [x] CC/CV state handling
- [x] SOC module
- [x] Fan control
- [x] TL494 control interface
- [x] AC event handling
- [x] Thermal protection logic
- [x] Fault-state handling
- [x] Charge-complete state
- [x] Cooperative periodic scheduler

## License

No license has been selected yet.

If this repository is intended to be open source, add a suitable `LICENSE` file before allowing reuse or redistribution.

## Author

**Gautam**  
B.Tech Electronics and Communication Engineering  
Embedded Systems / Firmware Development

---

This repository documents the firmware implementation of a custom EV charger controller developed for the PY32F002B microcontroller platform.
