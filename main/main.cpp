#include "radio-control.h"

#include <cstdio>
#include <string>

int main(int argc, char* argv[])
{
    bool debug = false;
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--debug") debug = true;
        else arguments.emplace_back(argv[i]);
    }
    if (arguments.size() > 2) {
        std::fprintf(stderr, "Usage: %s [--debug] [--list|--first|display-name|http://IP/fsapi] [PIN]\n", argv[0]);
        return 2;
    }
    radio_control::SocketTransport transport;
    transport.set_debug(debug);
    transport.log_debug("Debug logging enabled (stdout); HTTP query parameters and response bodies are omitted");
    radio_control::Radio radio(transport, arguments.size() > 1 ? arguments[1] : "1234");
    std::string target = !arguments.empty() ? arguments[0] : "--first";
    if (target == "--list" || target == "--first") {
        const bool list_only = target == "--list";
        std::vector<radio_control::RadioInfo> radios;
        if (!radio.discover(radios)) {
            std::fprintf(stderr, "Discovery failed: %s\n", radio.error().c_str());
            return 1;
        }
        if (radios.empty()) {
            std::puts("No radios found via SSDP.");
            return list_only ? 0 : 1;
        }
        for (std::size_t i = 0; i < radios.size(); ++i) {
            const auto& info = radios[i];
            std::printf("%zu. %s - %s\n", i + 1,
                info.name.empty() ? "(unknown name)" : info.name.c_str(), info.endpoint.c_str());
            if (!info.error.empty()) std::printf("   Name lookup: %s\n", info.error.c_str());
        }
        if (list_only) return 0;
        std::puts("Connecting to the first radio...");
        target = radios.front().endpoint;
    }
    const bool connected = target.compare(0, 7, "http://") == 0
        ? radio.connect_url(target) : radio.connect(target);
    if (!connected) {
        std::fprintf(stderr, "Connection failed: %s\n", radio.error().c_str());
        return 1;
    }
    std::printf("Connected: %s\n", radio.endpoint().c_str());
    bool on = false;
    unsigned volume = 0;
    if (!radio.power(on) || !radio.volume(volume)) {
        std::fprintf(stderr, "Read failed: %s\n", radio.error().c_str());
        (void)radio.disconnect();
        return 1;
    }
    std::printf("Power: %s; volume: %u\n", on ? "on" : "standby", volume);
    if (!radio.disconnect()) {
        std::fprintf(stderr, "Disconnect failed: %s\n", radio.error().c_str());
        return 1;
    }
    return 0;
}
