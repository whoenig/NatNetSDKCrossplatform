# NatNetSDKCrossplatform

This repository contains the direct depacketization method of the NatNet SDK to receive data from an OptiTrack Motion Capture system. The SDK can be found at https://optitrack.com/products/natnet-sdk/ and PacketClient helper was taken from this SDK (version 3.1). The portions of the SDK that have been used and are part of this repository are licensed under Apache License, Version 2.0. The remaining code is licensed under MIT.

This repository uses boost asio for communication.

## Build

Tested on Ubuntu 18.04

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
