# FlipBuzzer

FlipBuzzer is a Flipper Zero external app for driving a buzzer from GPIO `A7`, the internal speaker, or both at the same time. It includes a live frequency generator, built-in test sounds, playback for simple text-based sound files, and a Morse code sender.

<table width="100%">
  <tr>
    <th align="left">Release Download</th>
  </tr>
  <tr>
    <td>Download the packaged <code>.fap</code> from the latest release: <a href="https://github.com/jsammarco/FlipBuzzer/releases/tag/Release">FlipBuzzer Release</a></td>
  </tr>
</table>

## Screenshots

| Main Menu | Output Modes |
| --- | --- |
| ![Main Menu](Screenshots/Main%20Menu.png) | ![Output Modes](Screenshots/Output%20Modes.png) |
| Main navigation with the active output mode shown in the header. | Output destination selection for external, internal, or both. |

| Saved Sounds | Morse Code |
| --- | --- |
| ![Saved Sounds](Screenshots/Saved%20Screen.png) | ![Morse Code](Screenshots/Morse%20Code.png) |
| Built-in sounds plus the custom file browser entry. | Morse message editor and sender screen. |

## Features

- Select `External`, `Internal`, or `Both` output modes
- Live frequency generator with adjustable frequency and duty cycle
- Servo control on GPIO `A7`
- Built-in startup chime and alert beep
- File browser for custom `.fbsnd` sound sequences stored on the SD card
- Live file playback screen with progress, pause, stop, and LED blink feedback
- Morse code text entry and playback
- Startup sound on launch
- Helper PowerShell scripts for building and deploying from Windows

## Screens

### Main Menu

The main menu is the app hub. It contains:

- `Output Mode`
- `Frequency Generator`
- `Servo Control`
- `Saved Sounds`
- `Morse Code`
- `About`

The current output mode is shown in the top-right corner of the screen.

### Output Mode

Choose where sound is sent:

- `External (A7)`: PWM output on GPIO `A7`
- `Internal`: Flipper Zero internal speaker
- `Both`: external buzzer and internal speaker together

### Frequency Generator

The generator is for manually dialing in a tone and duty cycle.

- `Left` / `Right`: decrease or increase frequency
- `Up` / `Down`: increase or decrease duty cycle
- `OK`: start or stop playback
- Long `OK`: reset to the defaults
- `Back`: stop playback and return to the main menu

Defaults:

- Frequency: `1000 Hz`
- Duty cycle: `50%`

Ranges:

- Frequency: `20 Hz` to `20000 Hz`
- Duty cycle: `1%` to `99%`

Step size:

- Below `1000 Hz`, frequency changes in `10 Hz` steps
- At or above `1000 Hz`, frequency changes in `100 Hz` steps

### Servo Control

The servo screen is for positioning a standard PWM hobby servo on GPIO `A7`.

- `Left` / `Right`: move the target angle in `5` degree steps
- `OK`: start or stop holding the servo position
- Long `OK`: return to center
- `Back`: stop output and return to the main menu

Defaults:

- Center angle: `90 deg`

Ranges:

- Angle: `0 deg` to `180 deg`

Servo notes:

- Output is driven on `A7`
- The servo signal uses `50 Hz` PWM
- This screen is intended for external servo hardware, not the internal speaker

### Saved Sounds

This screen lets you play:

- `Startup Chime`
- `Alert Beep`
- `Browse Sound Files`

The browse option opens the Flipper file browser at:

`/ext/apps_data/flipbuzzer`

Only files ending in `.fbsnd` are shown.

When a custom sound file is playing, FlipBuzzer opens a playback screen that shows:

- The current file name
- Playback progress
- `OK` to pause or resume
- `Back` to stop playback and return
- Blinking LED feedback while the file is actively playing

### Morse Code

The Morse screen supports a short text message that can be sent as audio output.

- `OK`: open the text editor
- Long `OK`: transmit the current message as Morse code
- `Back`: return to the main menu

Default message:

- `SOS`

Supported characters include:

- `A-Z`
- `0-9`
- Common punctuation such as `. , ? ! / - ( ) : ; = + @ ' "`

Unsupported characters are skipped during playback.

### About

The about screen is a simple information page inside the app.

## Controls

### Main Menu

- `Up` / `Down`: move through menu items
- `OK`: open the selected screen
- `Back`: exit the app

### Output Mode

- `Up` / `Down`: cycle through output modes
- `OK`: confirm and return
- `Back`: return without changing screens

### Frequency Generator

- `Left` / `Right`: change frequency
- `Up` / `Down`: change duty cycle
- `OK`: toggle play and stop
- Long `OK`: reset frequency and duty cycle
- `Back`: stop output and return

### Servo Control

- `Left` / `Right`: change angle
- `OK`: toggle servo hold and stop
- Long `OK`: center the servo at `90 deg`
- `Back`: stop output and return

### Saved Sounds

- `Up` / `Down`: move through the list
- `OK`: play the selected sound or open the file browser
- `Back`: return to the main menu

### File Playback

- `OK`: pause or resume the current `.fbsnd` file
- `Back`: stop playback and return to `Saved Sounds`

### Morse Code

- `OK`: edit the message
- Long `OK`: send the message
- `Back`: return to the main menu

## Custom Sound File Format

Custom sound files use the `.fbsnd` extension and are read as plain text.

Each non-empty line can contain:

`frequency duration_ms duty`

Examples:

```text
440 150 50
660 150 50
880 300 60
```

Rules:

- The first value is frequency in Hz
- The second value is duration in milliseconds
- The third value is optional duty cycle in percent
- If duty is omitted, the app uses the default duty cycle of `50`
- Lines beginning with `#` are treated as comments
- Values can be separated by spaces, tabs, commas, or semicolons

Parsing and limits:

- Up to `128` tone steps are loaded
- Files larger than `4096` bytes are rejected
- Frequency is capped at `20000 Hz`
- Duty is clamped to `1` through `99`
- A line is ignored if it cannot be parsed into valid numeric values

The app creates the sound folder automatically if it does not exist:

`/ext/apps_data/flipbuzzer`

## PWM Ideas

FlipBuzzer's PWM output can be useful for quick bench testing and small hardware experiments. A few good starter uses:

- RC servos and ESCs: many expect a `50 Hz` signal with roughly `5%` to `10%` duty for position or throttle control
- LEDs: PWM is great for brightness control, blink patterns, and simple visual signaling
- Vibration motors: useful for haptic feedback tests or silent notification experiments
- Digital clocking: a steady PWM signal can act as a simple clock source for counters, shift registers, or logic testing
- Custom signal patterns: `.fbsnd` files can be used to play repeatable timing and frequency sequences for quick signal experiments

Tips:

- Start with low power test loads and add proper driver circuitry when needed
- Use `A7` for external PWM devices and make sure grounds are shared
- Do not drive larger motors, lasers, or other higher-current hardware directly from the Flipper pin

## Technical Notes

- External buzzer output uses Flipper PWM on `TIM1 PA7`
- Servo control also uses PWM on `TIM1 PA7` at `50 Hz`
- Internal speaker playback uses the Flipper speaker HAL
- Custom file playback can blink the Flipper LED while a file is actively playing
- Internal speaker volume is set to `0.8`
- The app attempts to acquire the internal speaker with a `50 ms` timeout
- The app stops active output when leaving the generator screen or exiting

## Project Layout

- `flipbuzzer.c` - main app implementation
- `application.fam` - Flipper app manifest and metadata
- `build.ps1` - mirrors the repo into a firmware tree and runs `fbt`
- `deploy_to_flipper.ps1` - uploads the built `.fap` to a connected Flipper and launches it
- `icon.png` - app icon used by the manifest
- `assets/splash_128x64.png` - bundled image asset
- `Screenshots/` - example UI screenshots

## Building

This repo is set up for the standard Flipper Zero external app workflow.

`build.ps1` does three things:

1. Resolves the source repo and firmware paths.
2. Mirrors this project into `applications_user/flipbuzzer` inside your firmware tree.
3. Runs `fbt` to build the app unless you tell it not to.

Default usage:

```powershell
.\build.ps1
```

Preview the mirror without copying, deleting, or building:

```powershell
.\build.ps1 -PreviewSync
```

Mirror the app but skip the actual build:

```powershell
.\build.ps1 -SkipBuild
```

Override the paths:

```powershell
.\build.ps1 `
  -SourceDir C:\Users\Joe\Projects\FlipBuzzer `
  -FirmwareDir C:\Users\Joe\Projects\flipperzero-firmware `
  -TargetDir C:\Users\Joe\Projects\flipperzero-firmware\applications_user\flipbuzzer
```

Important build behavior:

- The script refuses to mirror if source and target are the same path
- The target must stay inside the selected firmware directory
- The mirror excludes `.git`, `README.md`, and `build.ps1`
- The script removes a few stale legacy files before syncing

Default build output:

`C:\Users\Joe\Projects\flipperzero-firmware\build\f7-firmware-D\.extapps\flipbuzzer.fap`

## Deploying To Flipper

`deploy_to_flipper.ps1` uploads the built `.fap` to your device and then launches it.

Default usage:

```powershell
.\deploy_to_flipper.ps1 -Force
```

Defaults used by the script:

- Source file: `C:\Users\Joe\Projects\flipperzero-firmware\build\f7-firmware-D\.extapps\flipbuzzer.fap`
- Firmware dir: `C:\Users\Joe\Projects\flipperzero-firmware`
- Serial port: `COM17`
- Destination dir on Flipper: `/ext/apps/GPIO`

Example with explicit arguments:

```powershell
.\deploy_to_flipper.ps1 `
  -SourceFile C:\Users\Joe\Projects\flipperzero-firmware\build\f7-firmware-D\.extapps\flipbuzzer.fap `
  -FirmwareDir C:\Users\Joe\Projects\flipperzero-firmware `
  -Port COM17 `
  -DestinationDir /ext/apps/GPIO `
  -Force
```

The deploy script relies on the Flipper firmware helper scripts:

- `scripts/storage.py`
- `scripts/runfap.py`

It uses `py -3` when available, otherwise `python`.

## Requirements

To build and deploy from these scripts, you need:

- A local Flipper Zero firmware checkout
- The Flipper `fbt` build environment working from that firmware checkout
- Python available as `py -3` or `python`
- A connected Flipper Zero on the expected serial port

## Current App Metadata

- App ID: `flipbuzzer`
- Name: `FlipBuzzer`
- Category: `GPIO`
- Version: `0.1`
- Entry point: `flipbuzzer_app`

## Author

Created by ConsultingJoe.
