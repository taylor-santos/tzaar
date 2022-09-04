//
// Created by taylor-santos on 9/4/2022 at 01:02.
//

#pragma once

namespace STUN {

using ushort = asio::detail::u_short_type;
using ulong  = asio::detail::u_long_type;
using asio::detail::socket_ops::host_to_network_long;
using asio::detail::socket_ops::host_to_network_short;
using asio::detail::socket_ops::network_to_host_long;
using asio::detail::socket_ops::network_to_host_short;

using udp = asio::ip::udp;

struct IPAndPort {
    std::string ip;
    uint16_t    port = 0;
};

bool
stun_request(
    asio::io_context &ctx,
    udp::socket      &socket,
    std::string_view  stun_server_ip,
    std::string_view  stun_server_port,
    IPAndPort        &result);

constexpr auto kBindingRequest   = asio::detail::u_short_type{0x0001};
constexpr auto kBindingResponse  = asio::detail::u_short_type{0x0101};
constexpr auto kMappedAddress    = asio::detail::u_short_type{0x0001};
constexpr auto kXorMappedAddress = asio::detail::u_short_type{0x0020};

#ifdef _MSC_VER
#    define PACKED
#    pragma pack(push, 1)
#else
#    define PACKED __attribute__((__packed__))
#endif

// https://datatracker.ietf.org/doc/html/rfc5389#section-6
struct Request {
    Request();

    ushort                  message_type   = host_to_network_short(kBindingRequest);
    ushort                  message_length = host_to_network_short(0x0000);
    ulong                   magic_cookie   = host_to_network_long(0x2112A442);
    std::array<uint8_t, 12> transaction_id;
} PACKED;
static_assert(std::has_unique_object_representations_v<Request>, "Request must be unpadded");

// https://datatracker.ietf.org/doc/html/rfc5389#section-7
struct Response {
    ushort                    stun_message_type;
    ushort                    message_length;
    ulong                     magic_cookie;
    std::array<uint8_t, 12>   transaction_id;
    std::array<uint8_t, 1000> attributes;
} PACKED;
static_assert(std::has_unique_object_representations_v<Response>, "Response must be unpadded");

#ifdef _MSC_VER
#    pragma pack(pop)
#endif
#undef PACKED

class STUNRequest {
public:
    STUNRequest(
        asio::io_context &ctx,
        udp::socket      &socket,
        std::string_view  stun_server_ip,
        std::string_view  stun_server_port);

    STUNRequest(STUNRequest &&other) noexcept;

    ~STUNRequest();

    void
    run();

    bool
    done() const;

    std::optional<IPAndPort>
    result(std::chrono::milliseconds timeout);

    std::optional<IPAndPort>
    result() const;

    void
    cancel();

    [[nodiscard]] std::string
    address() const;

private:
    void
    handle_resolve(const asio::error_code &ec, const udp::resolver::results_type &endpoints);

    void
    handle_send(const asio::error_code &ec, size_t bytes);

    void
    handle_receive(const Response &response);

    using key_type = std::pair<asio::ip::address, asio::ip::port_type>;
    static std::map<key_type, STUNRequest *> requests_;
    static std::shared_mutex                 map_mutex_;

    udp::socket             &socket_;
    std::string_view         ip_;
    std::string_view         port_;
    udp::resolver            resolver_;
    Request                  request_;
    Response                 response_;
    asio::const_buffer       send_buf_;
    asio::mutable_buffer     receive_buf_;
    udp::endpoint            endpoint_;
    std::atomic_bool         done_ = false;
    std::optional<IPAndPort> result_;
};

} // namespace STUN
