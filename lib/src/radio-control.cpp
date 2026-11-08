#include "radio-control.h"
#include "protocol.h"

#include <algorithm>
#include <utility>

namespace radio_control {
namespace {
std::string encode(const std::string& value)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string result;
    for (char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            result += ch;
        } else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }
    return result;
}
}

Radio::Radio(Transport& transport, std::string pin)
    : transport_(transport), pin_(std::move(pin)) {}

bool Radio::request(const std::string& operation, std::string& xml, bool session)
{
    error_.clear();
    if (session && !connected()) {
        error_ = "Not connected";
        return false;
    }
    std::string url = endpoint_ + '/' + operation + "?pin=" + encode(pin_);
    if (session) url += "&sid=" + session_id_;
    if (!transport_.get(url, xml, error_)) {
        transport_.log_debug("FSAPI " + operation + " failed: " + error_);
        return false;
    }
    const auto status = detail::text(xml, "status");
    transport_.log_debug("FSAPI " + operation + " status=" + (status.empty() ? "(missing)" : status));
    if (status != "FS_OK") {
        error_ = status.empty() ? "Malformed FSAPI response: missing status" : status;
        if (status == "FS_INVALID_SESSION") session_id_.clear();
        return false;
    }
    return true;
}

bool Radio::connect_url(const std::string& endpoint)
{
    session_id_.clear();
    endpoint_ = endpoint;
    while (!endpoint_.empty() && endpoint_.back() == '/') endpoint_.pop_back();
    detail::Url parsed;
    if (!detail::parse_url(endpoint_, parsed)) {
        error_ = "Expected an HTTP FSAPI endpoint";
        return false;
    }
    std::string xml;
    if (!request("CREATE_SESSION", xml, false)) return false;
    const auto id = detail::text(xml, "sessionId");
    unsigned ignored = 0;
    if (!detail::number(id, ignored)) {
        error_ = "Malformed FSAPI session ID";
        return false;
    }
    session_id_ = id;
    transport_.log_debug("FSAPI session established");
    return true;
}

bool Radio::discover(std::vector<RadioInfo>& radios)
{
    radios.clear();
    error_.clear();
    std::vector<std::string> locations;
    if (!transport_.search(locations, error_)) return false;
    transport_.log_debug("Inspecting " + std::to_string(locations.size()) + " SSDP description URLs");
    std::string last_error;
    for (const auto& location : locations) {
        std::string xml;
        if (!transport_.get(location, xml, last_error)) {
            transport_.log_debug("Skipping device description: " + last_error);
            continue;
        }
        Radio candidate(transport_, pin_);
        candidate.endpoint_ = detail::text(xml, "webfsapi");
        while (!candidate.endpoint_.empty() && candidate.endpoint_.back() == '/') candidate.endpoint_.pop_back();
        detail::Url parsed;
        if (!detail::parse_url(candidate.endpoint_, parsed)) {
            last_error = "Invalid advertised FSAPI endpoint";
            transport_.log_debug("Skipping description: missing or invalid webfsapi element");
            continue;
        }
        if (std::any_of(radios.begin(), radios.end(), [&](const RadioInfo& info) {
                return info.endpoint == candidate.endpoint_;
            })) {
            transport_.log_debug("Skipping duplicate advertised FSAPI endpoint");
            continue;
        }
        transport_.log_debug("Advertised FSAPI endpoint: " + candidate.endpoint_);
        RadioInfo info;
        info.endpoint = candidate.endpoint_;
        info.name = detail::text(xml, "friendlyName");
        if (candidate.request("GET/netRemote.sys.info.friendlyName", xml, false)) {
            const auto name = detail::text(xml, "c8_array");
            if (!name.empty()) info.name = name;
            else info.error = "Missing display name in FSAPI response";
        } else {
            info.error = candidate.error();
        }
        transport_.log_debug("Discovered radio: " + (info.name.empty() ? "(unknown name)" : info.name));
        if (!info.error.empty()) transport_.log_debug("Name lookup failed: " + info.error);
        radios.push_back(std::move(info));
    }
    if (radios.empty() && !last_error.empty()) {
        error_ = last_error;
        return false;
    }
    error_.clear();
    transport_.log_debug("Discovery complete: " + std::to_string(radios.size()) + " radios");
    return true;
}

bool Radio::connect(const std::string& display_name)
{
    session_id_.clear();
    endpoint_.clear();
    std::vector<RadioInfo> radios;
    if (!discover(radios)) return false;
    for (const auto& info : radios) {
        if (info.name == display_name) return connect_url(info.endpoint);
    }
    endpoint_.clear();
    error_ = "Radio '" + display_name + "' not found via SSDP";
    return false;
}

bool Radio::disconnect()
{
    if (!connected()) { error_.clear(); return true; }
    std::string xml;
    const bool ok = request("DELETE_SESSION", xml, true);
    session_id_.clear();
    transport_.log_debug("Local session cleared");
    return ok;
}

bool Radio::read_number(const std::string& node, unsigned& value)
{
    std::string xml;
    if (!request("GET/" + node, xml, true)) return false;
    auto scalar = detail::text(xml, "u8");
    if (scalar.empty()) scalar = detail::text(xml, "u32");
    if (!detail::number(scalar, value)) {
        error_ = "Malformed numeric FSAPI value";
        return false;
    }
    return true;
}

bool Radio::power(bool& on)
{
    unsigned value = 0;
    if (!read_number("netRemote.sys.power", value)) return false;
    if (value > 1) { error_ = "Invalid power value"; return false; }
    on = value != 0;
    return true;
}

bool Radio::volume(unsigned& level)
{
    return read_number("netRemote.sys.audio.volume", level);
}
} // namespace radio_control
