# NatNetSDKCrossplatform

This repository contains the direct depacketization method of the NatNet SDK to receive data from an OptiTrack Motion Capture system. The official SDK is only available in a binary distribution. In contrast, the code here is fully open-source. The SDK can be found at https://optitrack.com/products/natnet-sdk/ and PacketClient helper was taken from this SDK (version 4.0, Windows). The portions of the SDK that have been used and are part of this repository are licensed under Apache License, Version 2.0. The remaining code is licensed under MIT.

This repository uses boost asio for communication.

This is just for testing new SDK versions. To actually use OptiTrack, use https://github.com/IMRCLab/libmotioncapture instead.

## Layout

`include`: Official include files from NaturalPoint
`samples`: Official samples (PacketClient from the Windows version of the SDK) and SampleClient from the Linux version
`src`: The actual source code of the crossplatform port, based on the depacketization method.

## Build

Tested on Ubuntu 20.04

```
mkdir build
cd build
cmake ..
make
```

## Run

1. Test command channel:

```
./natnettest <IP-where-motive-is-running>
```

2. Test data (multicast):

```
./mcr <IP-where-motive-is-running>
```

## Notes

There are two communication channels:

* Command (to send commands over UDP)
* Data (UDP multicast receiver)

This assumes the following default settings:

* multicast address: 239.255.42.99
* command port: 1510
* data port: 1511
