#pragma once
#include <string>

namespace radio_control { namespace detail {
struct Url {
    std::string host;
    std::string port;
    std::string path;
};
bool parse_url(const std::string& url, Url& result);
bool number(const std::string& text, unsigned& result);
std::string text(const std::string& xml, const std::string& tag);
std::string header(const std::string& headers, const std::string& name);
// Known Frontier discovery targets, including the modern UNDOK service.
constexpr const char* ssdp_services[] = {
    "urn:schemas-frontier-silicon-com:undok:fsapi:1",
    "urn:schemas-frontier-silicon-com:fs_reference:fsapi:1"
};
std::string ssdp_location(const std::string& response);
bool http_body(const std::string& response, std::string& body, std::string& error);
} }
