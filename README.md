# RIOT demo applications

Small applications to show what RIOT can do: on two Adafruit boards with a
screen, the CLUE and the PyBadge, on the nodes of the FIT IoT-LAB testbed,
and with BLE.

| Application         | Board              | What it does                                       |
|---------------------|--------------------|----------------------------------------------------|
| `clue-soundbars`    | `adafruit-clue`    | Live spectrum, level meter and history of the mic  |
| `clue-keywords`     | `adafruit-clue`    | Color Words: keyword spotting taught on the board  |
| `clue-sensors`      | `adafruit-clue`    | Sensor Deck: climate, light and compass pages      |
| `clue-meteor-lanes` | `adafruit-clue`    | Meteor Lanes: dodge meteors with the two buttons   |
| `pybadge-stackfall` | `adafruit-pybadge` | Stackfall: falling blocks puzzle with music        |
| `pybadge-tiltmaze`  | `adafruit-pybadge` | Tilt Maze: roll a ball through random mazes        |

And eight applications with machine learning, using emlearn, each with a
README of its own:

| Application          | Board        | What it does                                             |
|----------------------|--------------|----------------------------------------------------------|
| `digit-oracle`       | both         | Handwritten digits: MNIST over USB or drawn on the board |
| `vibe-guard`         | both         | Vibration anomalies, Gaussian mixture learned on board   |
| `spell-caster`       | both         | Gestures cast spells, random forest, record and train    |
| `bot-brain`          | both         | Pong against a network trained by imitation              |
| `clue-sound-spotter` | `adafruit-clue` | Claps, snaps, whistles, knocks; record and train      |
| `clue-color-sniffer` | `adafruit-clue` | Colors taught on the board, naive Bayes             |
| `clue-voice-keyboard` | `adafruit-clue` | Spell with the NATO alphabet, types over USB HID   |
| `disco-digit-draw`   | `stm32f723e-disco` | Draw a digit on the touch screen, recognized by the Digit Oracle network |

And two applications for IEEE 802.15.4 nodes of the FIT IoT-LAB testbed
(`iotlab-m3` by default, also `samr21-xpro` and `nrf52840dk`):

| Application          | What it does                                                    |
|----------------------|-----------------------------------------------------------------|
| `radio-jam-detector` | Interference on the 16 channels, Gaussian mixture learned on the node, with a test transmitter |
| `radio-tinyfl`       | Federated naive Bayes: nodes share model statistics, never samples; runs on native nodes too |

And five applications with BLE (NimBLE) and a phone or the BLE nodes of
IoT-LAB:

| Application          | Board           | What it does                                          |
|----------------------|-----------------|-------------------------------------------------------|
| `clue-bthome`        | `adafruit-clue` | BTHome v2 sensor beacon for Home Assistant, buttons and vibration anomalies |
| `clue-ble-dashboard` | `adafruit-clue` | Web Bluetooth page on the phone: live sensors, teach colors, message, piano |
| `clue-ble-motion`    | `adafruit-clue` | Turn a 3D teapot in a web page by turning the board, orientation filter on the board |
| `clue-qr-printer`    | `adafruit-clue` | QR code of a text from the shell or BLE, on the screen and the thermal printer of Adafruit |
| `ble-zones`          | `dwm1001`, `nrf52dk`, `nrf52840dk`, `adafruit-clue` | Zone of BLE tags from fingerprints, kNN learned on the node |

The two radio applications ran on IoT-LAB M3 nodes, and `ble-zones` on the
BLE nodes of IoT-LAB Toulouse and Saclay, see their READMEs. The others have
not been run on hardware yet: they build, and their machine learning and
protocol parts pass host tests (`make -C <app>/tests/host`),
`radio-tinyfl` also runs on native nodes (`make -C
radio-tinyfl/tests/native`), and `clue-qr-printer` with its printer on a
pseudo terminal (`clue-qr-printer/tests/native/run.py`).

The microphone applications (`clue-soundbars`, `clue-keywords`,
`clue-sound-spotter` and `clue-voice-keyboard`) also build for the Adafruit
EdgeBadge, a PyBadge with a PDM microphone (`BOARD=adafruit-pybadge`). This
is untested, and their screens are laid out for the 240x240 display of the
CLUE, not the 160x128 one of the badge.

## RIOT tree

The applications expect RIOT next to this directory (`../RIOT`), or in
`RIOTBASE`. They need changes that are not upstream yet, all gathered on
the `demo-apps` branch of RIOT, checked out in `../RIOT-drivers`:

- `pkg/lvgl: update to v9.6.0`, for `clue-soundbars`, `clue-keywords` and
  `clue-sensors`
- `pkg/emlearn: bump to 0.21.1`, for the machine learning applications
- `drivers/lis3dh: add I2C support` and `drivers/sn74hc165: initial
  support`, for the PyBadge accelerometer and buttons
- `boards/adafruit-clue` and `boards/adafruit-pybadge` configuration fixes,
  see below
- `drivers/adafruit_thermal: add the thermal printers of Adafruit`, for
  `clue-qr-printer`
- `cpu/samd5x: add a PDM driver on a SERCOM in SPI mode` and
  `boards/adafruit-pybadge: add the PDM microphone of the EdgeBadge`, for the
  microphone applications on the EdgeBadge

Build and flash with:

    make -C clue-soundbars RIOTBASE=$(pwd)/../RIOT-drivers flash term

`clue-meteor-lanes` also builds on master.

## Python tools

The machine learning applications come with Python tools, to train their
models and talk to the boards. They need a venv at the top of this
directory, with the emlearn version of the RIOT package:

    python3 -m venv .venv
    .venv/bin/pip install emlearn==0.21.1 scikit-learn numpy pyserial matplotlib setuptools

The host test of `clue-bthome` also needs `bthome-ble`, the BTHome parser of
Home Assistant, the one of `clue-ble-dashboard` needs node, and the native
test of `clue-qr-printer` needs `zxing-cpp` and `pillow`.

The training scripts call the C code of the applications through ctypes,
so they need a C compiler (`cc`).

## Shared modules

`modules/` holds four external modules used by the applications:

- `demo_gfx`: filled rectangles, a 5x7 font, character sprites and pixel
  blocks on top of `disp_dev`, for the applications that do not use lvgl.
- `demo_tone`: square wave melodies and sound effects on a GPIO, driven by
  two ztimers. Effects pause the melody, which then goes on.
- `demo_board`: the same button API on both boards (A and B on the CLUE,
  the 8 buttons of the PyBadge read with the `sn74hc165` driver), the
  speaker pin and amplifier, and a workaround for the CLUE microphone.
- `demo_accel`: the same motion sensor API on both boards, the LSM6DS of
  the CLUE (accelerometer and gyroscope) and the LIS3DH of the PyBadge
  (accelerometer). The axes of the tilt on the screen are unverified,
  `ACCEL_SWAP_XY`, `ACCEL_SIGN_X` and `ACCEL_SIGN_Y` change them.

## Board notes

### Adafruit CLUE

- The `periph_conf.h` of the board on master declares P0.00 as SCL of the
  first I2C bus, but P0.00 is the data pin of the PDM microphone, and
  `i2c_init()` leaves that TWIM enabled on the pin at boot. The
  `demo-apps` branch removes that bus (the edge connector
  shares the bus of the sensors). With the old configuration, the
  microphone applications call `board_mic_prepare()`, which disables any
  I2C bus on the microphone pin before `pdm_init()`.
- The speaker on P1.00 is driven as a GPIO, the board has no PWM
  configuration.
- The `demo-apps` branch adds the configuration of the LSM6DS
  accelerometer and gyroscope (I2C address 0x6A).
- The display runs with a 10 MHz SPI clock (8 MHz on the nRF52) instead of
  the default 5 MHz, see `Makefile.common`.

### Adafruit PyBadge

- The board documentation still says the display is not supported. It
  builds with `st7735` through `disp_dev`.
- The bit of each button (`BUTTON_MASK_*` in the board.h of the
  `demo-apps` branch) comes from the Adafruit Arcada library.
  `tests/drivers/sn74hc165` prints the bits as the buttons are pressed, to
  check them.
- The speaker is driven as a GPIO on its DAC pin (PA02), with the amplifier
  enabled on PA27.
- The EdgeBadge has a PDM microphone on SERCOM3 (clock PA16, data PA18).
  The SAMD51 has no PDM peripheral: the driver of the `demo-apps` branch
  clocks the microphone as an SPI master and filters the bits in software.
  The sample rate is the nearest one the SPI clock allows (16304 Hz for
  16 kHz); the applications use the rate returned by `pdm_init()`.

## The applications

### Sound Bars (`clue-soundbars`)

The PDM microphone runs at 16 kHz. A thread takes frames of 256 samples
(16 ms), removes the DC offset, applies a Hann window and runs the emlearn
FFT, then sums the power into 16 bands spaced logarithmically from 125 Hz to
7.6 kHz. lvgl shows them 30 times per second as bars with falling peak
markers. Button A switches between the spectrum, a level meter and the
level history, button B steps the microphone gain through 0, +10 and
+20 dB.

### Color Words (`clue-keywords`)

Keyword spotting without a dataset: you teach the board four words (RED,
GREEN, BLUE, YELLOW, or any four words you like) and it recognizes your
voice saying them. Saying a word paints the screen with its color.

- Button A selects a word, button B records the next utterance as an
  example of it. Up to 3 examples per word, the oldest is replaced. Press B
  again to cancel.
- `kws.c` detects utterances with an energy threshold that follows the noise
  floor, computes log mel features (20 bands every 32 ms) with the emlearn
  FFT and mel filterbank, removes the mean of each band, and compares the
  utterance to the examples with dynamic time warping.
- A word is accepted when its distance is below 2.2 times the spread of its
  own examples and below 0.75 times the distance of the second best word.
  The serial console prints every decision with the distances; tune
  `KWS_SPREAD_FACTOR`, `KWS_MARGIN` and `KWS_MAX_DIST` from there.
- The examples live in RAM and are lost at reset.
- `tests/host` runs the same code on the computer with synthetic sounds:
  `make -C clue-keywords/tests/host EMLEARN=<path to emlearn/emlearn>`.

### Sensor Deck (`clue-sensors`)

Three pages fed by SAUL: temperature (arc and 2 minute history), humidity
and pressure; ambient light, color and proximity from the APDS9960; and a
compass from the LIS3MDL, which calibrates itself while you turn the board
around. Buttons A and B change the page.

### Meteor Lanes (`clue-meteor-lanes`)

The ship flies in one of three lanes: A moves it left, B right. Meteors
fall faster and faster, stars give 5 bonus points. Sound effects on the
speaker, best score kept until reset.

### Stackfall (`pybadge-stackfall`)

A falling blocks puzzle with its own colors and an original tune. Left and
right move, down drops faster, up drops at once, A and B turn, START pauses,
SELECT turns the music on or off. Full rows flash and disappear; the level
goes up every 10 rows and the pieces fall faster.

### Tilt Maze (`pybadge-tiltmaze`)

Tilt the PyBadge to roll the ball into the green hole. Each level is a new
random maze with smaller cells; from level 3, red holes send the ball back
to the start with 3 seconds of penalty. B sets the current position as flat,
START pauses, SELECT mutes the sound.

The LIS3DH is read with the `lis3dh` driver, which the board configures on
I2C at address 0x19. If the ball rolls the wrong way, pass
`-DTILT_SWAP_XY=1`, `-DTILT_SIGN_X=-1` or `-DTILT_SIGN_Y=-1` in the
environment, for example:

    CFLAGS="-DTILT_SIGN_X=-1" make -C pybadge-tiltmaze RIOTBASE=... flash
