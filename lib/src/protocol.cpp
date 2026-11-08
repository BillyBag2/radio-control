#include "protocol.h"

#include <cctype>
#include <limits>

namespace radio_control { namespace detail {
namespace {
std::string trim(const std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::string lower(std::string value)
{
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
}

bool number(const std::string& value, unsigned& result)
{
    if (value.empty()) return false;
    unsigned parsed = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<unsigned>(c - '0');
        if (parsed > (std::numeric_limits<unsigned>::max() - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    result = parsed;
    return true;
}

bool parse_url(const std::string& url, Url& result)
{
    if (url.compare(0, 7, "http://") != 0 || url.find_first_of("\r\n\t #@") != std::string::npos)
        return false;
    const auto slash = url.find('/', 7);
    const auto authority = url.substr(7, slash == std::string::npos ? slash : slash - 7);
    const auto colon = authority.find(':');
    result.host = authority.substr(0, colon);
    result.port = colon == std::string::npos ? "80" : authority.substr(colon + 1);
    result.path = slash == std::string::npos ? "/" : url.substr(slash);
    unsigned port = 0;
    return !result.host.empty() && number(result.port, port) && port > 0 && port <= 65535;
}

// Extract the small, unnamespaced scalar elements used by FSAPI. No DTD processing.
std::string text(const std::string& xml, const std::string& tag)
{
    const auto open = '<' + tag + '>';
    const auto start = xml.find(open);
    if (start == std::string::npos) return {};
    const auto end = xml.find("</" + tag + '>', start + open.size());
    if (end == std::string::npos) return {};
    auto value = trim(xml.substr(start + open.size(), end - start - open.size()));
    if (value.compare(0, 9, "<![CDATA[") == 0 && value.size() >= 12 &&
        value.compare(value.size() - 3, 3, "]]>") == 0)
        return value.substr(9, value.size() - 12);
    const char* entities[] = {"&lt;", "&gt;", "&quot;", "&apos;", "&amp;"};
    const char* replacements[] = {"<", ">", "\"", "'", "&"};
    for (unsigned i = 0; i < 5; ++i) {
        std::size_t pos = 0;
        const std::string entity = entities[i];
        while ((pos = value.find(entity, pos)) != std::string::npos) {
            value.replace(pos, entity.size(), replacements[i]);
            ++pos;
        }
    }
    return value;
}

std::string header(const std::string& headers, const std::string& name)
{
    std::size_t start = headers.find("\r\n");
    while (start != std::string::npos) {
        start += 2;
        const auto end = headers.find("\r\n", start);
        const auto line = headers.substr(start, end == std::string::npos ? end : end - start);
        const auto colon = line.find(':');
        if (colon != std::string::npos && lower(trim(line.substr(0, colon))) == name)
            return trim(line.substr(colon + 1));
        start = end;
    }
    return {};
}

std::string ssdp_location(const std::string& response)
{
    if (response.compare(0, 13, "HTTP/1.1 200 ") != 0 &&
        response.compare(0, 13, "HTTP/1.0 200 ") != 0) return {};
    const auto service = header(response, "st");
    for (const auto* supported : ssdp_services) {
        if (service == supported) {
            const auto location = header(response, "location");
            Url parsed;
            return parse_url(location, parsed) ? location : std::string{};
        }
    }
    return {};
}

bool http_body(const std::string& response, std::string& body, std::string& error)
{
    body.clear();
    const auto end = response.find("\r\n\r\n");
    if (end == std::string::npos ||
        (response.compare(0, 9, "HTTP/1.0 ") != 0 && response.compare(0, 9, "HTTP/1.1 ") != 0) ||
        response.size() < 13 || response[12] != ' ') {
        error = "Malformed HTTP response";
        return false;
    }
    if (response.substr(9, 3) != "200") {
        error = "HTTP " + response.substr(9, 3);
        return false;
    }
    const auto headers = response.substr(0, end + 2);
    const auto payload = response.substr(end + 4);
    const auto transfer = lower(header(headers, "transfer-encoding"));
    if (!transfer.empty()) {
        if (transfer != "chunked") { error = "Unsupported HTTP transfer encoding"; return false; }
        std::size_t pos = 0;
        while (pos < payload.size()) {
            const auto line = payload.find("\r\n", pos);
            if (line == std::string::npos) break;
            auto size_text = payload.substr(pos, line - pos);
            size_text = size_text.substr(0, size_text.find(';'));
            std::size_t size = 0;
            if (size_text.empty()) break;
            for (char c : size_text) {
                const auto digit = std::string("0123456789abcdef").find(
                    static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if (digit == std::string::npos || size > 32768 / 16) {
                    error = "Invalid HTTP chunk size"; return false;
                }
                size = size * 16 + digit;
            }
            pos = line + 2;
            if (size == 0 && payload.find("\r\n", pos) != std::string::npos) return true;
            if (size > payload.size() - pos || payload.compare(pos + size, 2, "\r\n") != 0) break;
            body.append(payload, pos, size);
            pos += size + 2;
        }
        error = "Truncated chunked HTTP response";
        body.clear();
        return false;
    }
    const auto length_text = header(headers, "content-length");
    if (!length_text.empty()) {
        unsigned length = 0;
        if (!number(length_text, length) || payload.size() != length) {
            error = "Invalid or truncated HTTP content length";
            return false;
        }
    }
    body = payload;
    return true;
}
} }
