# Decibel Meter

A sound level meter built on an ESP32-S3. A MEMS microphone feeds a round 1.28" display that shows the current level as a filled arc gauge, a numeric readout, a 30-second average marker, and a 'max hold' marker. A push button resets the max and average markers.


> **Status:** Working on a breadboard. 3D-printed enclosure: [in progress].
> **Accuracy:** Approximate. This is not a calibrated or certified meter (see [Limitations](#limitations)).

## Features

- Filled circular gauge, 30 to 100 dB range, 270° sweep, with green / yellow / red zones
- Large numeric readout (5-second average)
- 30-second average marker and max-hold marker on the gauge
- Reset button that clears the max and average, with an on-screen 'RESET' confirmation
- Smoothed readings so the gauge doesn't jitter

## Parts

| Part | Notes |
|---|---|
| All from aliexpress except for switch was already on hand. Approximate cost $15
| ESP32-S3-N16R8 dev board | 16 MB flash storage, 8 MB PSRAM (extra memory) |
| 1.28" round TFT display | GC9A01 driver chip, 240×240 pixels, SPI connection | comes with wire harness
| INMP441 microphone | Digital MEMS mic that talks over I2S |
| 4-pin tactile pushbutton | Momentary switch |
| Breadboard and jumper wires | 
| 3D-printed enclosure | [planned] |

## Wiring

GPIO means general-purpose input/output pin on the ESP32.

**Display**

| Display pin | ESP32-S3 pin |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCL (SPI clock) | GPIO12 |
| SDA (SPI data) | GPIO11 |
| CS (chip select) | GPIO10 |
| DC (data/command) | GPIO9 |
| RES (reset) | GPIO14 |

**Microphone**

| Mic pin | ESP32-S3 pin |
|---|---|
| VDD | 3V3 |
| GND | GND |
| SCK (bit clock) | GPIO4 |
| WS (word select) | GPIO5 |
| SD (serial data) | GPIO6 |
| L/R (channel select) | GND |

**Reset button**

| Button pin | ESP32-S3 pin |
|---|---|
| One corner | GPIO2 |
| Opposite corner | GND |

## Build and flash

1. Install the Arduino IDE and add ESP32 board support (Espressif) through Boards Manager.
2. Install the **GFX Library for Arduino** from Library Manager.
3. Open `DecibelMeter.ino`.
4. Tools → Board → **ESP32S3 Dev Module**. If you upload through the board's native USB port, set Tools → **USB CDC On Boot** to Enabled so Serial Monitor works.
5. Select the port and upload.

## How it works

1. The mic sends 16,000 audio samples per second over I2S.
2. The sketch reads 512 samples at a time and computes their RMS.
3. It converts that to decibels with 20 × log10(RMS), subtracts a calibration offset, and applies exponential smoothing so each new reading only nudges the value.
4. Readings are averaged over 2 seconds for the gauge, 5 seconds for the center number, and 30 seconds for the average marker. The max marker tracks the loudest averaged value since power-up or the last reset. The time ratio seems to provide data without becoming overwhelming. 
5. The display library has no filled-arc function, so the gauge is drawn as many thin triangles positioned with sine and cosine.
6. The reset button clears the max and average.

## Limitations

- **Approximate calibration.** I calibrated against a phone sound-meter app 'dB Meter' at two points (a quiet room at about 37 dB and normal conversation at about 55 dB) and used the average offset. This provides a calibration precise enough for general readings.
- **No A-weighting.** Standard sound level meters use A-weighting, a filter that matches how sensitive human ears are at different pitches. This meter measures all frequencies equally.
- **Not for safety or compliance use.** Don't use it for hearing-safety or workplace noise decisions. It's a novelty only.

## Problems I ran into

- **No serial output on the S3.** The USB CDC On Boot setting and the choice between the native USB port and the UART port decide where Serial output appears.
- **Noisy mic readings.** I ruled out wiring with isolation tests (grounding the data pin, covering the mic, temporarily disabling the screen), then found that frequent screen redraws added noise and make the data displayed almost usable. Redrawing less often and averaging fixed most of it.
- **Negative dB values.** My first calibration offset was a guess. Comparing against a phone app at two points gave a more sensible one.
- **Unreliable reset button.** Slow screen redraws blocked the loop, so quick presses were missed. The fix was handling the button with an interrupt and redrawing once per loop pass.
- **"Bad CPU type" error on an Apple Silicon Mac.** Installing Rosetta (Apple's translation layer for Intel programs) fixed it. This proved to be a surprisingly difficult hiccup but an easy fix once the problem was narrowed down. 

## What I learned

- Reading digital audio over I2S and turning samples into a decibel value
- How RMS and the logarithmic decibel scale work
- Drawing graphics with trigonometry
- Non-blocking timing with `millis()` instead of `delay()`
- Interrupts and debouncing a button
- Calibrating a sensor against a reference, and being honest about its accuracy

## AI assistance

I built this project with help from Claude. I'm learning to program, and I used Claude to help draft code and explain concepts as I went. I chose the hardware, wired and soldered everything, tested every change on the real device, debugged problems, and made the design decisions.

## Credits

- [GFX Library for Arduino](https://github.com/moononournation/Arduino_GFX) by moononournation
- ESP_I2S, part of the Arduino-ESP32 core by Espressif
- Arduino IDE

Each library is used under its own license. See their repositories for details.


Built by Will A.
