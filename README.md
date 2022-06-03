# Beyond Protocol

![pipeline status](https://gitlab.utu.fi/nicolas.pope/beyond-protocol/badges/main/pipeline.svg)
![Latest Release](https://gitlab.utu.fi/nicolas.pope/beyond-protocol/-/badges/release.svg)

["Beyond"](https://sites.utu.fi/bittip/) is a remote presence research project to develop an immersive sense of presence using 3D and multi-sensory capture. This library provides the core network protocol and file format allowing you to connect to our web services, other nodes or load the capture files with a simple API that provides asynchonous packet callbacks. Utility functions are also provided, along with a thread pool that is used for processing each packet. An API using remote procedure calls is also available for communicating between other nodes or the web service.

A description of the [protocol](https://gitlab.utu.fi/nicolas.pope/beyond-protocol/-/wikis/Protocol-Definition) can be found on our wiki.

The library requires C++ 17.

## Installation

### Dependencies
* [msgpack v3](https://github.com/msgpack/msgpack-c) (note: version 4 doesn't work due to boost)
* liburiparser
* gnutls (optional, for TLS on websockets)

### Linux
Use the DEB package on supporting systems, or build from source using

```bash
mkdir build
cd build
cmake ..
make
sudo make install
```

### Windows
Download the ZIP containing the binaries and place the includes and static library in an appropriate build location.

## Usage
Check the `examples` directory in the repository to see how the library could be used, or checkout the [doxygen documentation](https://nicolaspope.utugit.fi/beyond-protocol/).

A simple example of opening an FTL file would be:

```cpp
auto stream = ftl::getStream("~/Videos/mycapture.ftl");

// Callback is triggered across a thread pool at the correct playback speed
// Each frame can consist of multiple packets with the same timestamp
stream->onPacket([](const StreamPacket &spkt, const Packet &pkt) {
    // Do something with the data packets
    return true;
});

stream->begin();
// Do something else, begin is not blocking.
stream->end();
```

## Roadmap
The longer term plan for this library is to:
* provide a full set of service RPC calls
* increase the space and decode efficiency of the protocol format.
* Allow for the use of UDP
* Possibly move channel type information into this library
* Support other file formats or streaming protocols
* Support other languages (Javascript, C#, Python)

It will not provide video encode or decode functionality and will remain lightweight and cross-platform.

## Authors and acknowledgment
This library has been developed by the following individuals:
* Nicolas Pope (lead) - nicolas.pope@utu.fi
* Sebastian Hahta
* Jason Mendes

Funding has been provided by:
* University of Turku, Finland
* Academy of Finland (grant)

## License
This library is open source under the MIT license, see LICENSE file.
