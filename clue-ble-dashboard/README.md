# CLUE BLE Dashboard

The Adafruit CLUE and a phone, over BLE: a web page in Chrome on Android
connects to the board with Web Bluetooth. No app to install.

On the page:

- **How far is the board**: a rough distance from the signal strength of
  the connection, also shown on the screen of the board. Walk away with the
  phone and watch it grow.
- **Motion**: the accelerometer, 10 times per second, and the A and B
  buttons of the board.
- **Around the board**: temperature, humidity, pressure and light (the
  clear channel of the color sensor, in counts).
- **Teach colors**: hold an object against the color sensor (above the
  screen) and tap a circle: the board takes 10 samples and learns the
  color with an emlearn Gaussian naive Bayes model, the one of
  [Color Sniffer](../clue-color-sniffer). Then it recognizes it: the circle
  lights up on the phone and the board plays its note. Press and hold a
  circle to forget its color.
- **Message on the screen**: up to 60 characters on the screen of the
  board.
- **Play the buzzer**: a small piano.

## Build and flash

    make RIOTBASE=<RIOT tree with the demo-apps branch> flash

The board advertises as `CLUE Dashboard`.

## Open the page

Web Bluetooth needs a secure page: served over https, or from `localhost`.
The simplest is to serve `web/` from the PC and forward the port to the
phone over USB with adb:

    python3 -m http.server -d web 8000
    adb reverse tcp:8000 tcp:8000

then open `http://localhost:8000` in Chrome on the phone and tap Connect.
The page also works from any https host, GitHub Pages for example, and in
Chrome on a PC with Bluetooth (on Linux, enable
`chrome://flags/#enable-experimental-web-platform-features`).

## The GATT service

Service `c1ae0001-5d8a-4a43-9a3c-6f5c2b1e7d90`, characteristics
`c1ae00XX-...` with:

| XX | Access       | Content                                                    |
|----|--------------|------------------------------------------------------------|
| 02 | read, notify | sensors, 20 bytes, 10 Hz: acceleration (mg), temperature (0.01 C), humidity (0.01 %), pressure (10 Pa), light, red, green, blue |
| 03 | read, notify | status, 11 bytes, 10 Hz: recognized color, its distance, RSSI, proximity, samples per color, buttons and teaching flag |
| 04 | write        | text for the screen                                        |
| 05 | write        | teach: command (1 teach, 2 forget, 3 forget all), color    |
| 06 | write        | note: frequency (50 to 5000 Hz) and duration (ms, at most 2000), 16 bits each |

All the values are little endian, see `protocol.h` and `web/protocol.js`.
No pairing: anyone near the board can connect to it.

## Host test

    make -C tests/host

Encodes packets in C and decodes them with the `protocol.js` of the page
(needs node), and checks the cleaning of the texts for the screen.

## Hardware notes

Not run on hardware yet, the page was only tried in headless Chrome with a
simulated board. Things to check first:

- notifications at 10 Hz with the default connection interval of Android
- the distance from the RSSI (-59 dBm at 1 m is a guess for the CLUE)
