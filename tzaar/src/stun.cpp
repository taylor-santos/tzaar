//
// Created by taylor-santos on 9/4/2022 at 01:03.
//

#include <array>
#include <random>
#include <algorithm>
#include <iostream>
#include <map>
#include <optional>
#include <shared_mutex>
#include <sstream>

#if _MSC_VER
#    include <sdkddkver.h>
#endif
#include <asio.hpp>
#include <iomanip>

#include "stun.hpp"

namespace STUN {

std::map<STUNRequest::key_type, STUNRequest *> STUNRequest::requests_;
std::shared_mutex                              STUNRequest::map_mutex_;

STUNRequest::STUNRequest(
    asio::io_context &ctx,
    udp::socket      &socket,
    std::string_view  stun_server_ip,
    std::string_view  stun_server_port)
    : socket_{socket}
    , ip_{stun_server_ip}
    , port_{stun_server_port}
    , resolver_(ctx) {}

void
STUNRequest::run() {
    using namespace std::placeholders;
    resolver_.async_resolve(
        udp::v4(),
        ip_,
        port_,
        std::bind(&STUNRequest::handle_resolve, this, _1, _2));
}

STUNRequest::STUNRequest(STUNRequest &&other) noexcept
    : socket_{other.socket_}
    , ip_{other.ip_}
    , port_{other.port_}
    , resolver_{std::move(other.resolver_)}
    , request_{other.request_}
    , response_{other.response_}
    , send_buf_{std::move(other.send_buf_)}
    , receive_buf_{std::move(other.receive_buf_)}
    , endpoint_{std::move(other.endpoint_)}
    , done_{other.done_.load()}
    , result_{std::move(other.result_)} {}

STUNRequest::~STUNRequest() {
    cancel();
}

void
STUNRequest::handle_resolve(
    const asio::error_code            &ec,
    const udp::resolver::results_type &endpoints) {
    if (ec) {
        std::cerr << ip_ << ":" << port_ << " - " << ec.message() << std::endl;
        done_ = true;
        return;
    }
    if (endpoints.empty()) {
        std::cerr << ip_ << ":" << port_ << " - Failed to resolve endpoint" << std::endl;
        done_ = true;
        return;
    }
    auto endpoint = endpoints->endpoint();
    {
        auto lock = std::unique_lock(map_mutex_);
        auto key  = key_type{endpoint.address(), endpoint.port()};
        auto it   = requests_.find(key);
        if (it != requests_.end()) {
            std::cerr << "[ERROR] overwriting " << endpoint << std::endl;
        }
        requests_[key] = this;
    }
    send_buf_    = asio::buffer(&request_, sizeof(request_));
    receive_buf_ = asio::buffer(&response_, sizeof(response_));

    using namespace std::placeholders;
    socket_
        .async_send_to(send_buf_, endpoint, 0, std::bind(&STUNRequest::handle_send, this, _1, _2));
    socket_.async_receive_from(receive_buf_, endpoint_, [&](const asio::error_code &ec, size_t) {
        if (ec) {
            std::cerr << ec.message() << std::endl;
            return;
        }
        auto lock = std::shared_lock(map_mutex_);
        auto key  = key_type{endpoint_.address(), endpoint_.port()};
        auto it   = requests_.find(key);
        if (it == requests_.end()) {
            return;
        }

        it->second->handle_receive(response_);
    });
}

void
STUNRequest::handle_send(const asio::error_code &ec, size_t bytes) {
    if (ec) {
        std::cerr << ip_ << ":" << port_ << " - " << ec.message() << std::endl;
        done_ = true;
        return;
    }
    if (bytes != 20) {
        std::cerr << ip_ << ":" << port_ << " - Header is 20 bytes long but " << bytes
                  << " were sent" << std::endl;
        done_ = true;
        return;
    }
}

void
STUNRequest::handle_receive(const Response &response) {
    if (response.transaction_id != request_.transaction_id) {
        std::cerr << ip_ << ":" << port_ << " - Incorrect transaction ID on STUN response"
                  << std::endl;
        std::cerr << "Received ";
        for (auto &byte : response.transaction_id) {
            std::cerr << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        std::cerr << std::endl;
        done_ = true;
        return;
    }
    if (response.stun_message_type != kBindingResponse) {
        std::cerr << ip_ << ":" << port_ << " - Incorrect message type on STUN response"
                  << std::endl;
        done_ = true;
        return;
    }

    auto attributes_length =
        std::min(static_cast<size_t>(response.message_length), response.attributes.size());
    auto  &attributes = response.attributes;
    size_t i          = 0;
    while (i < attributes_length) {
        auto type   = network_to_host_short(*reinterpret_cast<const ushort *>(&attributes[i]));
        auto length = network_to_host_short(*reinterpret_cast<const ushort *>(&attributes[i + 2]));
        if (type == kXorMappedAddress) {
            auto port =
                network_to_host_short(*reinterpret_cast<const ushort *>(&attributes[i + 6]));
            port ^= 0x2112;
            auto ip = std::to_string(attributes[i + 8] ^ 0x21) + "." +
                      std::to_string(attributes[i + 9] ^ 0x12) + "." +
                      std::to_string(attributes[i + 10] ^ 0xA4) + "." +
                      std::to_string(attributes[i + 11] ^ 0x42);

            result_ = {ip, port};
            done_   = true;
            return;
        } else if (type == kMappedAddress) {
            auto port =
                network_to_host_short(*reinterpret_cast<const ushort *>(&attributes[i + 6]));
            auto ip = std::to_string(attributes[i + 8]) + "." + std::to_string(attributes[i + 9]) +
                      "." + std::to_string(attributes[i + 10]) + "." +
                      std::to_string(attributes[i + 11]);
            result_ = {ip, port};
            done_   = true;
            break;
        }
        i += 4 + length;
    }
}

bool
STUNRequest::done() const {
    return done_;
}

std::optional<IPAndPort>
STUNRequest::result(std::chrono::milliseconds timeout) {
    using namespace std::chrono;
    auto start = high_resolution_clock::now();
    while (!done_) {
        if (high_resolution_clock::now() >= start + timeout) {
            std::cerr << ip_ << ":" << port_ << " - Timeout exceeded\n";
            cancel();
            break;
        }
        using namespace std::literals;
        std::this_thread::sleep_for(500ms);
    }
    return result_;
}

std::optional<IPAndPort>
STUNRequest::result() const {
    while (!done_) {
        using namespace std::literals;
        std::this_thread::sleep_for(500ms);
    }
    return result_;
}

void
STUNRequest::cancel() {
    asio::error_code ignore;
    socket_.cancel(ignore);

    auto key = key_type{endpoint_.address(), endpoint_.port()};
    auto it  = requests_.find(key);
    if (it != requests_.end()) {
        requests_.erase(it);
    }

    done_ = true;
}

std::string
STUNRequest::address() const {
    std::stringstream ss;
    ss << ip_ << ":" << port_;
    return ss.str();
}

Request::Request() {
    static auto rd   = std::random_device();
    static auto rand = std::mt19937(rd());
    static auto dist = std::uniform_int_distribution<int>(256);

    std::generate(transaction_id.begin(), transaction_id.end(), [&] {
        return static_cast<uint8_t>(dist(rand));
    });
}

} // namespace STUN
