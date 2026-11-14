# BLE Zones

In which zone of a building is a BLE tag? Fixed boards, the anchors,
measure the signal strength of the tags. A locator learns, from examples,
the zone that goes with each pattern of signal strengths (a fingerprint),
with the k nearest neighbors of emlearn on the device, and then locates the
tags. No map, no positions, no path loss model: only examples.

Made for the BLE nodes of the FIT IoT-LAB testbed (dwm1001 in Toulouse,
nrf52dk and nrf52840dk in Saclay), runs on the Adafruit CLUE too.

## How it works

Every node runs the same firmware, in one of three roles (`role`
command):

- **anchor**, the default: scans the tags and advertises, every second,
  the mean signal strength of each tag heard during that second
- **tag**: advertises its id 10 times per second
- **off**: only listens

Every node is also a locator. It hears the reports of the anchors and
keeps, for each tag, the latest signal strength measured by each anchor
(-100 dBm when an anchor did not hear it for 3 seconds): the fingerprint
of the tag, one number per anchor, in the order the anchors were first
heard.

`teach <tag> <zone>` adds the next 15 fingerprints of a tag to the model,
with that zone. Then, every second, the node gives the zone of each tag:
the zone of most of the 5 learned fingerprints closest to its fingerprint.

    Z <tag> <zone> <votes out of 5> <anchors that hear the tag>

All the messages are BLE advertisements: manufacturer data with the test
company id 0xFFFF, see `zones.h`. No connection, no IP.

## Shell commands

| Command                     | What it does                                       |
|-----------------------------|----------------------------------------------------|
| `role off\|anchor\|tag`     | role of the node                                   |
| `teach <tag> <zone> [s]`    | learn the fingerprints of a tag in a zone (0 to 9) |
| `forget`                    | forget the zones, the anchors and the tags         |
| `status`                    | id of the node, anchors, tags, fingerprints        |
| `verbose on\|off`           | print the reports (`A`) and fingerprints (`F`)     |

The id of a node is the end of its BLE address, shown at boot and by
`status`.

## On IoT-LAB

The commands are to be run by you, not by the tools of this repository.

### Toulouse: 3 zones over a floor

The dwm1001 nodes of Toulouse are spread over a floor of about 22 x 12 m.
Split it along x in 3 zones (x < 3 m, 3 to 10 m, above 10 m), with 5
anchors per zone and the other nodes as tags, half of them to teach and
half to test:

    make RIOTBASE=<RIOT tree with the demo-apps branch> BOARD=dwm1001
    iotlab-experiment submit -n zones -d 60 -l toulouse,dwm1001,<nodes>,bin/dwm1001/ble-zones.elf
    iotlab-experiment wait

One tunnel per node, from your machine (a port per node, for example 22000
plus the node number):

    ssh -N -L 127.0.0.1:22001:dwm1001-1:20000 -L ... <login>@toulouse.iot-lab.info

Then `tools/run_zones.py` drives the experiment from a plan, a JSON list of
the nodes with their local port, role (`anchor`, `train` or `test`) and
zone. Each training node in turn becomes a tag and teaches its zone to the
locator; then the test nodes become tags, 3 at a time, and the script
counts the seconds located in the right zone:

    python3 tools/run_zones.py plan.json --locator dwm1001-21 --log zones.log
    iotlab-experiment stop

The test places are never used for teaching: the score says how well the
zones are found at places the model never saw.

### Saclay: 2 groups of nodes

The nrf52dk and nrf52840dk nodes of Saclay form two groups about 12 m
apart, a zone each. The two boards need their own firmware:

    make RIOTBASE=<RIOT tree> BOARD=nrf52dk
    make RIOTBASE=<RIOT tree> BOARD=nrf52840dk
    iotlab-experiment submit -n zones -d 30 \
        -l saclay,nrf52dk,1-10,bin/nrf52dk/ble-zones.elf \
        -l saclay,nrf52840dk,1-10,bin/nrf52840dk/ble-zones.elf

## At home, with a phone

Any board of the list can be an anchor, the CLUE included. The phone can be
the tag with an advertiser app, nRF Connect for Android for example:
manufacturer data with company id `0xFFFF` and data `5A54` followed by a
tag id of 2 bytes, little endian (`5A543412` is the tag `1234`). Walk to
each room with the phone and teach it from the shell of a locator. Phones
advertise less often than the boards, check that the anchors hear it with
`verbose on`.

## Host test

    make -C tests/host

Encodes and parses the advertisements, then simulates a floor of 40 x 10 m
with 16 anchors, 3 zones and a log-distance path loss with shadowing and
noise: tags at 30 places teach the zones, tags at 30 other places are
located, every message going through the advertisement encoding.

## Hardware notes

Not run on hardware yet. Things to check first:

- that scanning and advertising at the same time loses few tag
  advertisements on the nRF52832 of the dwm1001
- the number of anchors heard per tag across the floor
