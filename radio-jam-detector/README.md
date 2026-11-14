# Jam Detector

Interference detection on the 16 IEEE 802.15.4 channels (11 to 26 at 2.4
GHz), with a model learned on the node. Made for the FIT IoT-LAB testbed,
where one node detects and another one can make some interference on
purpose.

## How it works

Twice a second, the node scans the 16 channels: 8 passes over all the
channels, with 3 clear channel assessments per visit at -85, -75 and -65
dBm. The occupancy of a channel is the share of busy assessments, from 0
(always clear) to 1 (always busy).

For the first minute the node learns: it keeps the scans, then fits a
Gaussian mixture (3 components, diagonal covariances) by expectation
maximization, on the node. After that, emlearn scores each scan against the
mixture. The level of a scan is how far its log-likelihood falls below the
usual ones (0 for usual scans); 3 scans in a row at level 3 or more raise
the alarm, 5 usual scans clear it.

The alarm also tells which channels are hit: those 4 standard deviations
or more above what was learned. One or two adjacent channels look like an
IEEE 802.15.4 transmitter (narrowband), three or more like a wider signal
such as Wi-Fi, which covers about 4 IEEE 802.15.4 channels (wideband).

Usual Wi-Fi or IEEE 802.15.4 traffic is learned as part of the environment:
only changes are flagged.

The radio is used through the IEEE 802.15.4 Radio HAL of RIOT, without
network stack. Supported boards: `iotlab-m3` (AT86RF231, the default),
`samr21-xpro` (AT86RF233) and `nrf52840dk`.

## Serial output

    L learning for 120 scans
    S 12 L 0 6a83000000000002 4 0
    L learned, usual occupancy 39 66 52 18 0 0 0 0 0 0 0 0 0 0 0 9 %
    S 130 D 4 6a83000000000002 2 0
    ALARM narrowband channels 20-20
    S 133 A 87 6a8300000f000002 1 7
    CLEAR

`S <scan> <state> <level> <occupancy> <frames> <crc errors>`: state L
(learning), D (detecting) or A (alarm), ten times the level, one hex digit
per channel from 11 to 26 (0: always clear, f: always busy), and the frames
received during the scan, valid and with a bad CRC.

## Shell commands

    learn [seconds]                              learn again, 60 s by default
    interfere <channel> [s] [duty %] [dBm]       send test frames
    scan                                         stop sending, scan again

`interfere` sends broadcast frames of 111 bytes on one channel, without
listening first, for up to 60 seconds (10 by default), a given share of the
time (50% by default), at -17 dBm by default. Then the node scans again,
with what it learned before.

The radio of a testbed is shared with the other experiments of the site:
keep the test transmissions short, at low power, and between nodes close to
each other.

## On IoT-LAB

Build, then submit an experiment with two M3 nodes next to each other
(the commands are to be run by you, not by the tools of this repository):

    make RIOTBASE=<RIOT tree with the demo-apps branch>
    iotlab-experiment submit -n jam -d 20 \
        -l grenoble,m3,100+102,bin/iotlab-m3/radio-jam-detector.elf
    iotlab-experiment wait

One tunnel per node to its serial port, from your machine:

    ssh -N -L 127.0.0.1:20100:m3-100:20000 <login>@grenoble.iot-lab.info
    ssh -N -L 127.0.0.1:20102:m3-102:20000 <login>@grenoble.iot-lab.info

Watch the detector, and once it has learned (about a minute), start the
test transmitter on the other node:

    python3 tools/watch.py --tcp 127.0.0.1:20100 --log jam.log
    python3 tools/watch.py --tcp 127.0.0.1:20102 --send "interfere 20 20 50"

Try a few channels, duty cycles and powers, then:

    iotlab-experiment stop

`tools/watch.py` shows one colored row per scan. It needs Python 3.10 or
later, and pyserial only for a local serial port (`--port`).

## Host test

    make -C tests/host

Synthetic scans of a usual environment (Wi-Fi on channels 11 to 14, an
IEEE 802.15.4 network on channel 26) are learned, then the test checks the
share of usual scans flagged, and that four interferences (a transmitter on
channel 20 at 50% and on channel 15 always on, Wi-Fi channels 6 and 11) are
flagged and described with the right kind and channels.

## Hardware notes

Not run on hardware yet. Things to check first:

- the time of a scan (8 x 16 channels x 3 assessments), which sets how
  often the node can scan
- that the occupancy is 0 on quiet channels and goes up near a transmitter
- the alarm level and the 4 standard deviations, on a real environment
