#include "radio-control.h"
#include "protocol.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
void check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct Reply { std::string url; std::string body; bool ok; };
class FakeTransport final : public radio_control::Transport {
public:
    std::vector<Reply> replies;
    std::vector<std::string> search_locations = {
        "http://192.0.2.1/description.xml", "http://192.0.2.2/description.xml"};
    bool search_ok = true;
    std::size_t next = 0;
    bool search(std::vector<std::string>& locations, std::string& error) override
    {
        locations = search_locations;
        error = search_ok ? "" : "SSDP search failed";
        return search_ok;
    }
    bool get(const std::string& url, std::string& body, std::string& error) override
    {
        check(next < replies.size(), "unexpected HTTP request");
        const auto& reply = replies[next++];
        check(url == reply.url, "incorrect FSAPI URL");
        body = reply.body;
        error = reply.ok ? "" : "Network timeout";
        return reply.ok;
    }
};
const std::string ok = "<fsapiResponse><status>FS_OK</status>";
}

int main()
{
    using namespace radio_control;
    FakeTransport transport;
    const std::string base = "http://192.0.2.2:80/fsapi";
    transport.replies = {
        {"http://192.0.2.1/description.xml", "<root><webfsapi>http://192.0.2.1/fsapi</webfsapi></root>", true},
        {"http://192.0.2.1/fsapi/GET/netRemote.sys.info.friendlyName?pin=1234", ok + "<value><c8_array>other</c8_array></value></fsapiResponse>", true},
        {"http://192.0.2.2/description.xml", "<root><webfsapi>" + base + "/</webfsapi></root>", true},
        {base + "/GET/netRemote.sys.info.friendlyName?pin=1234", ok + "<value><c8_array><![CDATA[william]]></c8_array></value></fsapiResponse>", true},
        {base + "/CREATE_SESSION?pin=1234", ok + "<sessionId>42</sessionId></fsapiResponse>", true},
        {base + "/GET/netRemote.sys.power?pin=1234&sid=42", ok + "<value><u8>1</u8></value></fsapiResponse>", true},
        {base + "/GET/netRemote.sys.audio.volume?pin=1234&sid=42", ok + "<value><u8>7</u8></value></fsapiResponse>", true},
        {base + "/DELETE_SESSION?pin=1234&sid=42", ok + "</fsapiResponse>", true}
    };
    Radio radio(transport);
    bool on = false;
    unsigned volume = 0;
    check(!radio.power(on), "reads before connecting must fail");
    check(radio.connect(), "discover william and connect");
    check(radio.connected() && radio.endpoint() == base, "session state");
    check(radio.power(on) && on, "read power");
    check(radio.volume(volume) && volume == 7, "read volume");
    check(radio.disconnect() && !radio.connected(), "disconnect");
    check(transport.next == transport.replies.size(), "consume expected requests");
    check(!radio.connect_url("https://192.0.2.2/fsapi"), "reject unsupported URL");
    check(!radio.connect_url("http://192.0.2.2:0/fsapi"), "reject invalid port");

    FakeTransport listing;
    listing.search_locations.push_back("http://192.0.2.2/duplicate.xml");
    Radio listed(listing);
    listing.replies = {
        {base + "/CREATE_SESSION?pin=1234", ok + "<sessionId>42</sessionId></fsapiResponse>", true},
        {"http://192.0.2.1/description.xml", "<root><webfsapi>http://192.0.2.1/fsapi</webfsapi></root>", true},
        {"http://192.0.2.1/fsapi/GET/netRemote.sys.info.friendlyName?pin=1234", ok + "<value><c8_array>kitchen</c8_array></value></fsapiResponse>", true},
        {"http://192.0.2.2/description.xml", "<root><friendlyName>william</friendlyName><webfsapi>" + base + "</webfsapi></root>", true},
        {base + "/GET/netRemote.sys.info.friendlyName?pin=1234", "", false},
        {"http://192.0.2.2/duplicate.xml", "<root><webfsapi>" + base + "/</webfsapi></root>", true},
        {base + "/DELETE_SESSION?pin=1234&sid=42", ok + "</fsapiResponse>", true}
    };
    check(listed.connect_url(base), "connection before listing");
    std::vector<RadioInfo> radios;
    check(listed.discover(radios) && radios.size() == 2, "list distinct radio endpoints");
    check(radios[0].name == "kitchen" && radios[0].error.empty(), "read discovered name");
    check(radios[1].name == "william" && radios[1].endpoint == base &&
          radios[1].error == "Network timeout", "retain radio with failed name lookup");
    check(listed.connected() && listed.endpoint() == base, "listing preserves existing connection");
    check(listed.disconnect(), "existing session still works after listing");
    check(listing.next == listing.replies.size(), "listing does not create sessions");
    listing.search_locations.clear();
    check(listed.discover(radios) && radios.empty(), "empty discovery clears previous list");
    listing.search_ok = false;
    check(!listed.discover(radios) && listed.error() == "SSDP search failed", "report discovery failure");

    FakeTransport failures;
    Radio failed(failures, "a&b");
    failures.replies = {
        {base + "/CREATE_SESSION?pin=a%26b", "<fsapiResponse><status>FS_FAIL</status></fsapiResponse>", true},
        {base + "/CREATE_SESSION?pin=a%26b", ok + "<sessionId>42x</sessionId></fsapiResponse>", true},
        {base + "/CREATE_SESSION?pin=a%26b", "", false},
        {base + "/CREATE_SESSION?pin=a%26b", ok + "<sessionId>42</sessionId></fsapiResponse>", true},
        {base + "/GET/netRemote.sys.audio.volume?pin=a%26b&sid=42", ok + "<value><u8>7x</u8></value></fsapiResponse>", true},
        {base + "/GET/netRemote.sys.power?pin=a%26b&sid=42", "<fsapiResponse><status>FS_INVALID_SESSION</status></fsapiResponse>", true}
    };
    check(!failed.connect_url(base) && failed.error() == "FS_FAIL", "propagate radio rejection");
    check(!failed.connect_url(base) && !failed.connected(), "reject invalid session ID");
    check(!failed.connect_url(base) && failed.error() == "Network timeout", "propagate transport failure");
    check(failed.connect_url(base), "reconnect after failure");
    volume = 19;
    check(!failed.volume(volume) && volume == 19, "malformed value must preserve output");
    check(!failed.power(on) && !failed.connected(), "expired session must clear state");

    std::string body, error;
    check(detail::http_body("HTTP/1.0 200 OK\r\nContent-Length: 3\r\n\r\nabc", body, error) && body == "abc", "HTTP content length");
    check(detail::http_body("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nab\r\n1\r\nc\r\n0\r\n\r\n", body, error) && body == "abc", "chunked HTTP");
    check(!detail::http_body("HTTP/1.0 403 Forbidden\r\n\r\n", body, error) && error == "HTTP 403", "HTTP error");
    check(!detail::http_body("HTTP/1.0 200 OK\r\nContent-Length: 4\r\n\r\nabc", body, error), "truncated response");
    check(!detail::http_body("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nabc", body, error), "truncated chunk");
    check(detail::header("HTTP/1.1 200 OK\r\nLoCaTiOn: http://192.0.2.2/dd.xml\r\n\r\n", "location") == "http://192.0.2.2/dd.xml", "SSDP case-insensitive headers");
    check(detail::text("<c8_array>william &amp; friends</c8_array>", "c8_array") == "william & friends", "XML entities");
    check(!detail::number("42949672960", volume), "numeric overflow");
    const std::string ssdp_headers = "\r\nLOCATION: http://192.0.2.2:2244/device\r\n\r\n";
    check(detail::ssdp_location("HTTP/1.1 200 OK\r\nST: urn:schemas-frontier-silicon-com:undok:fsapi:1" + ssdp_headers)
          == "http://192.0.2.2:2244/device", "accept modern UNDOK discovery response");
    check(detail::ssdp_location("HTTP/1.1 200 OK\r\nST: urn:schemas-frontier-silicon-com:fs_reference:fsapi:1" + ssdp_headers)
          == "http://192.0.2.2:2244/device", "accept legacy Frontier discovery response");
    check(detail::ssdp_location("HTTP/1.1 200 OK\r\nST: urn:schemas-upnp-org:device:MediaServer:1" + ssdp_headers).empty(),
          "reject unrelated SSDP services");
    check(detail::ssdp_location("HTTP/1.1 500 Error\r\nST: urn:schemas-frontier-silicon-com:undok:fsapi:1" + ssdp_headers).empty(),
          "reject unsuccessful SSDP response");
    std::puts("Radio protocol tests passed");
    return 0;
}
