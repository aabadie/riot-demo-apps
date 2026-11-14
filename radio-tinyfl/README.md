# TinyFL

Federated learning over the radio: IEEE 802.15.4 nodes learn together a
classifier that none of them could learn alone, without sending a single
sample. Made for the FIT IoT-LAB testbed, and runs on native nodes too.

## How it works

The dataset has 4 classes of 4 features (Gaussian blobs, the same on all
the nodes). Each node only sees 2 of the classes, chosen from its address,
and draws 4 new samples per second up to 200. Its local Gaussian naive
Bayes model knows only 2 classes: it is right on about half of a test set
of all the classes.

A Gaussian naive Bayes model is made of the count, mean and variance of
each feature for each class. The statistics of two nodes merge exactly into
the statistics of all their samples (the parallel variance of Chan, Golub
and LeVeque). So each node broadcasts its statistics, 78 bytes in UDP to
the link-local multicast address `ff02::1`, every 2 seconds. It keeps the
last statistics of each node it hears, with their sequence number, and
relays one of them every second, up to 4 hops from their origin. Its
federated model merges its own statistics with all of them: it is the model
it would have learned with the samples of all the nodes it heard (up to the
rounding of the 8.8 fixed point numbers of the messages), and emlearn
classifies with it.

After a reset, a node starts again with no samples and sequence number 0,
while the others still have its old statistics with a higher number. When
it hears them relayed back, it takes a number past theirs, so its new
statistics replace the old ones, as routing protocols do with their
sequence numbers.

This works for any model whose training can be summed up in statistics
that add up: naive Bayes, linear regression, Gaussian models of normal
behavior for anomaly detection. Neural networks need federated averaging
over several rounds instead.

## Serial output

    node 1031 sees classes 1 and 2
    R 1162 seq 3 hops 1, 1 nodes known
    A 1031 30 local 48.0 fed 87.5 nodes 4 samples 120 480 sent 45 received 130

`A <node> <s> local <%> fed <%> nodes <n> samples <own> <all> sent <n>
received <n>`, every 5 seconds: the accuracy of the local and federated
models on a test set of 200 samples, the nodes in the federation (the node
included), its own samples and those of the federation, the messages sent
and received.

## Run it on native nodes

The nodes meet through the ZEP dispatcher of RIOT. Build the dispatcher
and the application, start the dispatcher, then a few nodes, each in its
own terminal with its own EUI-64:

    make -C <RIOT>/dist/tools/zep_dispatch
    make RIOTBASE=<RIOT> BOARD=native64
    <RIOT>/dist/tools/zep_dispatch/bin/zep_dispatch ::1 17754
    bin/native64/radio-tinyfl.elf -z [::1]:17754 -U 02:00:00:00:00:00:00:01
    bin/native64/radio-tinyfl.elf -z [::1]:17754 -U 02:00:00:00:00:00:00:02

`make -C tests/native` does it all and checks the results (about 3
minutes): 4 nodes that all hear each other; 10 nodes on a line, where each
node must know the nodes up to 4 hops away and no more; 4 nodes, one of
them reset during the run, whose new statistics must reach the others.

## On IoT-LAB

Build for the M3 nodes, then submit an experiment with a few nodes (the
commands are to be run by you, not by the tools of this repository):

    make RIOTBASE=<RIOT tree with the demo-apps branch>
    iotlab-experiment submit -n tinyfl -d 20 \
        -l grenoble,m3,100-107,bin/iotlab-m3/radio-tinyfl.elf
    iotlab-experiment wait

One tunnel per node to its serial port, from your machine, for example:

    ssh -N -L 127.0.0.1:20100:m3-100:20000 \
           -L 127.0.0.1:20101:m3-101:20000 \
           -L 127.0.0.1:20102:m3-102:20000 <login>@grenoble.iot-lab.info

Then watch the federation grow:

    python3 tools/collect.py 20100 20101 20102 --log tinyfl.log

Nodes spread along a corridor do not all hear each other: the relaying
shows up in the number of nodes each one knows. Reset a node to see it
start again from nothing and catch up:

    iotlab-node --reset -l grenoble,m3,103
    iotlab-experiment stop

## Host test

    make -C tests/host

Checks that merged statistics are those of all the samples, that the
messages keep them, the table of the nodes heard, that 8 nodes seeing 2
classes each get a federated model exactly as good as one learned with all
the samples in one place, and the relaying over a line of nodes.

## Hardware notes

Runs on native nodes, not run on hardware yet. Things to check first:

- the messages get through on a busy site, with the default CSMA and
  retransmissions of RIOT
- how many nodes of a site hear each other
