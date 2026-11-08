#include "radio-control.h"
#include "protocol.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

namespace radio_control {
namespace {
using Clock = std::chrono::steady_clock;
class Socket {
public:
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() { if (fd_ >= 0) ::close(fd_); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    int fd() const { return fd_; }
private:
    int fd_;
};

bool wait_for(int fd, bool write, Clock::time_point deadline, std::string& error)
{
    if (fd < 0 || fd >= FD_SETSIZE) { error = "Invalid socket descriptor"; return false; }
    for (;;) {
        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now()).count();
        if (remaining <= 0) { error = "Network timeout"; return false; }
        timeval timeout{};
        timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(remaining / 1000000);
        timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>(remaining % 1000000);
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(fd, &ready);
        const int result = ::select(fd + 1, write ? nullptr : &ready,
                                    write ? &ready : nullptr, nullptr, &timeout);
        if (result > 0) return true;
        if (result < 0 && errno == EINTR) continue;
        error = result == 0 ? "Network timeout" : std::strerror(errno);
        return false;
    }
}

bool retryable() { return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK; }

}

bool SocketTransport::get(const std::string& url, std::string& body, std::string& error)
{
    log_debug("HTTP GET " + url.substr(0, url.find('?')) +
              " (query omitted), timeout=" + std::to_string(timeout_ms_) + " ms");
    const bool ok = get_impl(url, body, error);
    log_debug(ok ? "HTTP complete: " + std::to_string(body.size()) + " body bytes"
                 : "HTTP failed: " + error);
    return ok;
}

void SocketTransport::log_debug(const std::string& message) const
{
    if (!debug_) return;
    std::printf("[debug] %s\n", message.c_str());
    std::fflush(stdout);
}

bool SocketTransport::get_impl(const std::string& url, std::string& body, std::string& error)
{
    body.clear();
    error.clear();
    detail::Url parsed;
    if (timeout_ms_ <= 0 || !detail::parse_url(url, parsed)) {
        error = "Invalid URL or timeout (HTTP IPv4/hostname URLs supported)";
        return false;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const int resolved = ::getaddrinfo(parsed.host.c_str(), parsed.port.c_str(), &hints, &addresses);
    if (resolved != 0) { error = "Cannot resolve host: " + parsed.host; return false; }
    // DNS resolution uses the platform resolver's timeout, independently of this deadline.
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms_);
    int connected_fd = -1;
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
        const int fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) { log_debug("TCP socket failed: " + std::string(std::strerror(errno))); continue; }
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address->ai_addr);
        char ip[INET_ADDRSTRLEN] = {};
        if (::inet_ntop(AF_INET, &ipv4->sin_addr, ip, sizeof(ip)) != nullptr)
            log_debug("TCP connecting to " + std::string(ip) + ':' + parsed.port);
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            ::close(fd); continue;
        }
#ifdef SO_NOSIGPIPE
        const int enabled = 1;
        if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
            ::close(fd); continue;
        }
#endif
        const int result = ::connect(fd, address->ai_addr, static_cast<socklen_t>(address->ai_addrlen));
        if (result == 0 || (errno == EINPROGRESS && wait_for(fd, true, deadline, error))) {
            int socket_error = 0;
            socklen_t length = sizeof(socket_error);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &length) == 0 && socket_error == 0) {
                connected_fd = fd;
                log_debug("TCP connected");
                break;
            }
            if (socket_error != 0) error = std::strerror(socket_error);
        }
        log_debug("TCP connection attempt failed for " + parsed.host + ':' + parsed.port + ": " +
                  (error.empty() ? std::string(std::strerror(errno)) : error));
        ::close(fd);
    }
    ::freeaddrinfo(addresses);
    Socket socket(connected_fd);
    if (socket.fd() < 0) { error = "Cannot connect to " + parsed.host + ':' + parsed.port; return false; }
    const std::string request = "GET " + parsed.path + " HTTP/1.0\r\nHost: " + parsed.host + ':' +
        parsed.port + "\r\nConnection: close\r\nAccept: text/xml\r\n\r\n";
    std::size_t sent = 0;
    while (sent < request.size()) {
        if (!wait_for(socket.fd(), true, deadline, error)) return false;
#ifdef MSG_NOSIGNAL
        const int send_flags = MSG_NOSIGNAL;
#else
        const int send_flags = 0;
#endif
        const auto count = ::send(socket.fd(), request.data() + sent, request.size() - sent, send_flags);
        if (count < 0 && retryable()) continue;
        if (count <= 0) { error = "HTTP send failed"; return false; }
        sent += static_cast<std::size_t>(count);
    }
    std::string response;
    char buffer[1024];
    for (;;) {
        if (!wait_for(socket.fd(), false, deadline, error)) return false;
        const auto count = ::recv(socket.fd(), buffer, sizeof(buffer), 0);
        if (count < 0 && retryable()) continue;
        if (count < 0) { error = "HTTP receive failed"; return false; }
        if (count == 0) break;
        if (response.size() + static_cast<std::size_t>(count) > 32768) {
            error = "HTTP response exceeds 32 KiB"; return false;
        }
        response.append(buffer, static_cast<std::size_t>(count));
    }
    const auto status_end = response.find("\r\n");
    log_debug("HTTP response: " + (status_end == std::string::npos ? "(missing status line)" : response.substr(0, status_end)) +
              "; received " + std::to_string(response.size()) + " bytes");
    const bool ok = detail::http_body(response, body, error);
    if (ok) error.clear();
    return ok;
}

bool SocketTransport::search(std::vector<std::string>& locations, std::string& error)
{
    log_debug("SSDP search: 239.255.255.250:1900, OS-selected interface, timeout=" +
              std::to_string(timeout_ms_) + " ms");
    const bool ok = search_impl(locations, error);
    log_debug(ok ? "SSDP complete: " + std::to_string(locations.size()) + " unique description URLs"
                 : "SSDP failed: " + error);
    return ok;
}

bool SocketTransport::search_impl(std::vector<std::string>& locations, std::string& error)
{
    locations.clear();
    error.clear();
    if (timeout_ms_ < 1000) { error = "SSDP timeout must be at least 1000 ms"; return false; }
    Socket socket(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (socket.fd() < 0) { error = "Cannot create SSDP socket: " + std::string(std::strerror(errno)); return false; }
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(1900);
    destination.sin_addr.s_addr = ::inet_addr("239.255.255.250");
    const int mx = std::min(5, std::max(1, timeout_ms_ / 1000 - 1));
    // Repeat to tolerate packet loss; duplicate responses are discarded below.
    for (int i = 0; i < 2; ++i) {
        for (const auto* service : detail::ssdp_services) {
            const std::string request = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
                "MAN: \"ssdp:discover\"\r\nMX: " + std::to_string(mx) + "\r\nST: " + service + "\r\n\r\n";
            log_debug("SSDP TX M-SEARCH attempt=" + std::to_string(i + 1) +
                      " MX=" + std::to_string(mx) + " ST=" + service);
            const auto count = ::sendto(socket.fd(), request.data(), request.size(), 0,
                reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
            if (count < 0 || static_cast<std::size_t>(count) != request.size()) {
                error = "Cannot send SSDP search: " + std::string(std::strerror(errno)); return false;
            }
        }
    }
    sockaddr_in local{};
    socklen_t local_length = sizeof(local);
    if (::getsockname(socket.fd(), reinterpret_cast<sockaddr*>(&local), &local_length) == 0)
        log_debug("SSDP receiving on UDP port " + std::to_string(ntohs(local.sin_port)));
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms_);
    std::size_t received = 0;
    char buffer[4096];
    while (wait_for(socket.fd(), false, deadline, error)) {
        sockaddr_in peer{};
        socklen_t peer_length = sizeof(peer);
        const auto count = ::recvfrom(socket.fd(), buffer, sizeof(buffer), 0,
            reinterpret_cast<sockaddr*>(&peer), &peer_length);
        if (count < 0 && retryable()) continue;
        if (count <= 0) { error = "SSDP receive failed"; return false; }
        const std::string response(buffer, static_cast<std::size_t>(count));
        ++received;
        char ip[INET_ADDRSTRLEN] = {};
        (void)::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        const auto line_end = response.find("\r\n");
        log_debug("SSDP RX from " + std::string(ip) + ':' + std::to_string(ntohs(peer.sin_port)) +
                  ": " + std::to_string(count) + " bytes; " +
                  (line_end == std::string::npos ? "(missing status line)" : response.substr(0, line_end)));
        log_debug("SSDP RX ST=" + detail::header(response, "st") +
                  " LOCATION=" + detail::header(response, "location"));
        const auto location = detail::ssdp_location(response);
        if (location.empty()) log_debug("SSDP response ignored: unsupported service, invalid status, or invalid/missing LOCATION");
        if (!location.empty() &&
            std::find(locations.begin(), locations.end(), location) == locations.end()) {
            locations.push_back(location);
            log_debug("SSDP accepted description URL: " + location);
            if (locations.size() >= 16) break;
        } else if (!location.empty()) {
            log_debug("SSDP duplicate description URL ignored");
        }
    }
    if (error != "Network timeout" && !error.empty()) return false;
    log_debug("SSDP received " + std::to_string(received) + " datagrams" +
              (error == "Network timeout" ? "; search window expired" : "; result limit reached"));
    error.clear();
    return true;
}
} // namespace radio_control
