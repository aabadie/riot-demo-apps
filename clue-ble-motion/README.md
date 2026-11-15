# CLUE BLE Motion

Turn a teapot on a phone or a computer by turning the Adafruit CLUE. The
board computes its orientation from its gyroscope and accelerometer and
sends it over BLE; a web page draws a 3D teapot that follows it, with Web
Bluetooth. No app to install.

- The board reads the LSM6DS 100 times per second and runs the orientation
  filter of Madgwick (`fusion.c`). It sends a quaternion of 8 bytes, 25 to
  100 times per second.
- 6 axes by default: gyroscope and accelerometer. The tilt is always right,
  the heading drifts slowly; A, or Recenter on the page, sets it back.
- 9 axes, with B or on the page: the LIS3MDL magnetometer holds the heading
  to the north. Turn the board around in all directions first, it learns
  the offsets of the magnetometer.
- The screen of the board shows a spirit level and the angles.

The teapot is the Utah teapot of Martin Newell, from the
`TeapotGeometry` of three.js, drawn with a glazed material and its shadow.
three.js r160 is copied in `web/vendor/three` (MIT, see its README), so the
page needs nothing from the internet.

## Build and flash

    make RIOTBASE=<RIOT tree with the demo-apps branch> flash

The board advertises as `CLUE Motion`. Keep it still for 2 seconds after
it starts: the gyroscope is calibrated (the screen says when).

## Open the page

Web Bluetooth needs a secure page: served over https, or from `localhost`.
On a phone, serve `web/` from the PC and forward the port over USB:

    python3 -m http.server -d web 8000
    adb reverse tcp:8000 tcp:8000

then open `http://localhost:8000` in Chrome on the phone and tap Connect.
On a PC with Bluetooth, open `http://localhost:8000` in Chrome or Edge (on
Linux, Web Bluetooth may need
`chrome://flags/#enable-experimental-web-platform-features`). Firefox and
Safari have no Web Bluetooth. "Try without a board" turns the teapot by
itself, to see the page alone.

## The GATT service

Service `c1ae0101-5d8a-4a43-9a3c-6f5c2b1e7d90`, characteristics
`c1ae01XX-...` with:

| XX | Access | Content                                                     |
|----|--------|-------------------------------------------------------------|
| 02 | read   | info, 6 bytes: version (1), sensor rate (Hz, 16 bits), notification rate (Hz), features (bit 0: magnetometer), axes used (6 or 9) |
| 03 | notify | motion, 10 bytes: sequence number, flags (A, B, magnetometer used, calibrating), quaternion w x y z as 4 int16, 16384 is 1 |
| 04 | notify | raw, 12 bytes: acceleration (mg) and rotation speed (0.1 deg/s), 3 int16 each |
| 05 | write  | control: `[1]` recenter, `[2, 6 or 9]` axes, `[3, 25, 50 or 100]` rate, `[4]` calibrate the gyroscope |

All the values are little endian, see `protocol.h` and `web/protocol.js`.
No pairing: anyone near the board can connect to it.

- One motion packet fits a notification of the default MTU, no MTU
  exchange is needed.
- On connection, the board asks for a connection interval of 15 to 30 ms
  (the phone decides, the serial port prints what it chose).
- The page always draws the last orientation received; the sequence number
  gives the rate of the updates shown on the page.
- The quaternion turns the axes of the sensor into the earth axes, z up.
  The page maps them to its scene, y up: board x stays x, board z (out of
  the screen) becomes y, board y becomes -z (`toScene()` in
  `web/protocol.js`).
- Recentering only removes the heading, the tilt stays, so a board flat on
  the table gives an upright teapot.

## Host test

    make -C tests/host

- the gradient of the filter against a numerical one
- the gyroscope bias found while the board is still, and found again when
  it moved during the calibration
- 25 s of simulated motion, turns about each axis then about all of them,
  with a gyroscope bias, noise on all the sensors and a start from a wrong
  orientation: the tilt stays within 2 degrees, the heading drifts 0.1
  degree in 22 s in 6 axes, and is within 0.2 degree of the board in 9 axes
- the packets encoded in C and decoded with the `protocol.js` of the page
  (needs node), and the mapping of the axes to the scene

## Hardware notes

Not run on hardware yet. The page was tried in headless Chrome, with
"Try without a board" and with a simulated board. Things to check first:

- the axes: tilting the board right should tilt the teapot right. If an
  axis is mirrored or swapped, change `toScene()` in `web/protocol.js`.
- the axes of the magnetometer against the ones of the accelerometer are
  not known: if 9 axes makes the heading wander, try `MAG_SIGN_X`,
  `MAG_SIGN_Y` and `MAG_SIGN_Z` (-1) in `CFLAGS`. Indoors, iron and
  electronics bend the field, 6 axes and recentering can be better.
- the latency, motion to screen: about 30 to 60 ms is expected at a 15 to
  30 ms connection interval, not measured.
