# Radio control

A small C++11 library for connecting to a Pure Elan Connect over the local network.
It lists radios on the local network or discovers one by its UNDOK display name, opens an
FSAPI session, and reads power and volume without changing playback.

## Build and run on Linux

No third-party libraries are required; use CMake and a C++ compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
./build/main/radio-control
```

Or run `bash build.sh`. The default run lists discovered radios and connects to
the first result. Discovery order depends on SSDP response order. To list without
connecting:

```sh
bash build.sh --list
# Or, after building:
./build/main/radio-control --list
```

Use `--first` to explicitly list and connect to the first radio. Optional arguments select the display name and network
control PIN (default `1234`):

```sh
bash build.sh william 1234
```

Enable debug logging to stdout with `--debug` (it can appear before or after
the other arguments):

```sh
bash build.sh --debug --list
./build/main/radio-control --debug --first
./build/main/radio-control --debug http://192.168.1.20/fsapi
```

Debug output shows SSDP search targets, timeout, local UDP receive port, reply
addresses and headers, accepted/ignored replies, and total received datagrams.
It also shows TCP connection attempts, HTTP status and response sizes, advertised
FSAPI endpoints, name lookup failures, and session progress. HTTP query parameters
and response bodies are omitted to keep PINs and session IDs out of those logs.
Logging is disabled by default. To enable it when using the library directly,
call `transport.set_debug(true)` on your `SocketTransport`.

The display name is not a DNS hostname. Discovery uses SSDP multicast on UDP
239.255.255.250:1900, reads the advertised `webfsapi` endpoint, and matches
`netRemote.sys.info.friendlyName` exactly. Run on the radio's local network with
multicast allowed. If discovery is unavailable, supply the radio's IP directly:

```sh
./build/main/radio-control http://192.168.1.20/fsapi 1234
```

Discovery searches both Frontier service identifiers:
`urn:schemas-frontier-silicon-com:undok:fsapi:1` and the older
`urn:schemas-frontier-silicon-com:fs_reference:fsapi:1`. The advertised endpoint
supplies the HTTP port; it is not assumed to be port 80. Running inside WSL,
a container, or a virtual machine may require network configuration to carry
SSDP multicast between the virtual network and the radio's LAN.

When connecting, the example prints the current power and raw volume level, then deletes
its session. Failures print an error and return a nonzero exit status.

## Library usage

Link the `radio_control` CMake target and include `radio-control.h`:

```cpp
#include "radio-control.h"

radio_control::SocketTransport transport(3000); // timeout in milliseconds
radio_control::Radio radio(transport, "1234");

if (radio.connect("william")) {
    bool on = false;
    unsigned volume = 0;
    if (radio.power(on) && radio.volume(volume)) {
        // Use the current values here.
    }
    radio.disconnect();
} else {
    // Inspect radio.error().
}
```

`connect_url("http://IP/fsapi")` bypasses discovery. `connected()` indicates that
an FSAPI session was acquired; it does not monitor reachability. Reconnect if the
radio restarts or invalidates the session. Call `disconnect()` to release the
session; destruction does not make a network request.

`radio.discover(radios)` fills a `std::vector<radio_control::RadioInfo>` with each
radio's `name`, `endpoint`, and any name-lookup `error`. It returns true for a
completed search, including an empty list. Duplicate endpoints are removed.
Radios with a failed name lookup remain in the list, using the description's
friendly name when available. Listing does not create sessions or change an
existing connection. Discovery covers compatible FSAPI radios responding on the
local network; sleeping radios or blocked multicast may prevent detection.

The API is synchronous and not thread-safe. Keep the transport alive for as long
as the `Radio`. Each HTTP connection has a total timeout after DNS resolution;
DNS itself uses the platform resolver's timeout. Discovery waits for the search
timeout (at least 1000 ms), then probes up to 16 distinct advertised URLs, each
with its own HTTP timeout. Responses are limited to 32 KiB. The socket transport
supports IPv4/hostnames and plain HTTP, including content-length, close-delimited,
and chunked responses. The small FSAPI scalar parser handles ordinary elements,
CDATA, and the five named XML entities; it is not a general XML parser.

## ESP32-S3 and ESP32-P4 (ESP-IDF)

The same sources use ESP-IDF's supported BSD socket API through lwIP. There are
no Linux-only dependencies, exceptions, or RTTI requirements in the library.
Add this repository's `lib` directory to an existing ESP-IDF application's
component directories, before including ESP-IDF's project CMake file:

```cmake
set(EXTRA_COMPONENT_DIRS "/path/to/radio-control/lib")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_radio_controller)
```

In that application's `main/CMakeLists.txt`, depend on the component (its name is
`lib`, taken from the directory):

```cmake
idf_component_register(SRCS "main.cpp" REQUIRES lib)
```

Initialize networking in your application and wait for an IP address before
using the library. Call it from a task that can block. S3 can use its built-in
Wi-Fi; P4 needs Ethernet or a supported external Wi-Fi companion. Discovery uses
the default network interface. With an IP endpoint, no name resolver is needed.

Build the surrounding firmware using `idf.py set-target esp32s3` or
`idf.py set-target esp32p4`, then `idf.py build`. Use ESP-IDF menuconfig's compiler
optimization settings for firmware optimization; the desktop root CMake release
flags do not override the surrounding ESP-IDF project. ESP-IDF toolchains and
physical ESP boards have not been tested in this workspace.

## Build settings and tests

Release is the default desktop build type. GCC and Clang use `-O3` optimization
and retain debug symbols (`-g`). Optimized code can affect debugger stepping and
variable visibility.

All desktop build types and the ESP-IDF component enable strict GCC/Clang
warnings, treated as errors:

```text
-Wall -Wextra -Wpedantic -Werror -Wconversion
-Wsign-conversion -Wshadow -Wformat=2 -Wundef
```

Run the tests after building:

```sh
ctest --test-dir build --output-on-failure
```

Protocol tests exercise display-name matching, session handling, malformed
responses, and HTTP framing. If Python 3 is available, an integration test runs
the real example against a local simulated HTTP radio, checking session cleanup,
wrong-PIN errors, and response-size limits. These tests do not need a real radio.
Disable tests with `-DBUILD_TESTING=OFF` when embedding the desktop library.

## Protocol references

- [Pure confirms Elan Connect supports UNDOK](https://support.pure-audio.com/b/pure-support/en-US/kb/articles/is-there-an-application-for-elan-connect-connect).
- [FSAPI operations and node reference](https://github.com/flammy/fsapi/blob/master/FSAPI.md).
- [AFSAPI implementation: advertised webfsapi endpoints and session requests](https://sources.debian.org/src/python-afsapi/0.2.7-2/afsapi/api.py/).
- [ESP-IDF BSD sockets](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/lwip.html).
- [ESP32-P4 Wi-Fi expansion](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/wifi-expansion.html).
