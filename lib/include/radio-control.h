#pragma once

#include <string>
#include <vector>

namespace radio_control {

// Blocking transport. Implementations must bound responses and report failures.
class Transport {
public:
    virtual ~Transport() = default;
    virtual bool search(std::vector<std::string>& locations, std::string& error) = 0;
    virtual bool get(const std::string& url, std::string& body, std::string& error) = 0;
    virtual void log_debug(const std::string&) const {}
};

// BSD sockets on Linux and ESP-IDF/lwIP. No Wi-Fi initialization is performed.
class SocketTransport final : public Transport {
public:
    explicit SocketTransport(int timeout_ms = 3000) : timeout_ms_(timeout_ms) {}
    void set_debug(bool enabled) { debug_ = enabled; }
    void log_debug(const std::string& message) const override;
    bool search(std::vector<std::string>& locations, std::string& error) override;
    bool get(const std::string& url, std::string& body, std::string& error) override;
private:
    bool get_impl(const std::string& url, std::string& body, std::string& error);
    bool search_impl(std::vector<std::string>& locations, std::string& error);
    int timeout_ms_;
    bool debug_ = false;
};

struct RadioInfo {
    std::string name;
    std::string endpoint;
    // A failed name lookup does not hide a discovered endpoint.
    std::string error;
};

// Not thread-safe. The transport must outlive the radio.
class Radio {
public:
    explicit Radio(Transport& transport, std::string pin = "1234");
    // Does not create sessions or change this Radio's current connection.
    // Returns true for a completed search, including an empty result.
    bool discover(std::vector<RadioInfo>& radios);
    bool connect(const std::string& display_name = "william");
    // Bypass multicast discovery with a known endpoint, e.g. http://192.168.1.20/fsapi.
    bool connect_url(const std::string& endpoint);
    bool disconnect();
    bool power(bool& on);
    bool volume(unsigned& level);
    bool connected() const { return !session_id_.empty(); }
    const std::string& endpoint() const { return endpoint_; }
    const std::string& error() const { return error_; }
private:
    bool request(const std::string& operation, std::string& xml, bool session);
    bool read_number(const std::string& node, unsigned& value);
    Transport& transport_;
    std::string pin_;
    std::string endpoint_;
    std::string session_id_;
    std::string error_;
};

} // namespace radio_control
