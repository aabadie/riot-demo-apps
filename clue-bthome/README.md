# CLUE BTHome

The Adafruit CLUE as a [BTHome v2](https://bthome.io) sensor beacon: Home
Assistant finds it by itself, with no pairing, no connection and no
integration to write.

The board broadcasts, in non-connectable BLE advertisements:

- temperature, humidity, pressure and light, refreshed every 2 seconds.
  The APDS9960 has no lux conversion: the light is its clear channel, in
  counts, sent as illuminance
- the A and B buttons, as BTHome events: press, double press, triple press,
  long press
- "moving", from the accelerometer
- "vibration", raised when the vibration spectrum is unusual: the Gaussian
  mixture of [Vibe Guard](../vibe-guard), fitted with emlearn on the board
  during the first 30 seconds, then scored on every window. Put the board on
  a washing machine, a fan or a 3D printer and get a notification on the
  phone when it sounds wrong.

Hold A and B together for 2 seconds to learn the normal vibrations again.

A button event goes out with a packet id of its own, repeated for
1.5 seconds: Home Assistant takes each new packet id as new events, so the
sensor values that change in that time wait for the next packet id.

The screen shows the values sent, the last button events and the packet
id. The advertisement is also printed on the serial port, as `B <packet id>
<service data in hex>`.

## Build and flash

    make RIOTBASE=<RIOT tree with the demo-apps branch> flash

## On the phone

Home Assistant decodes BTHome on its server, from a Bluetooth adapter of the
server or from an ESPHome Bluetooth proxy: the CLUE shows up in Settings,
Devices, as a discovered BTHome device named `CLUE-xxxx`. The Home
Assistant Android app then shows its sensors, and automations can send a
notification to the phone on a button event or an unusual vibration. The
Android app does not receive BLE advertisements by itself.

Without Home Assistant, nRF Connect for Android shows the raw service data
(UUID 0xFCD2) of the advertisements.

## Host test

    make -C tests/host

Encodes a few advertisements and decodes them with
[bthome-ble](https://github.com/Bluetooth-Devices/bthome-ble), the parser of
Home Assistant (`pip install bthome-ble` in the venv of the repository),
and checks the button events.

## Hardware notes

Not run on hardware yet. Things to check first:

- the "moving" threshold, 0.03 g of standard deviation of the acceleration
- that Home Assistant sees the 2 buttons and the binary sensors
