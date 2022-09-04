#include "tzaar.hpp"

#include <iostream>
#include <utility>
#include <list>
#include <random>
#include <charconv>
//
//#include <SDL.h>

#if _MSC_VER
#    include <sdkddkver.h>
#endif
#include <asio.hpp>
#include <csignal>

#include <map>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include "stun.hpp"

const int SCREEN_WIDTH  = 720;
const int SCREEN_HEIGHT = 720;

// void
// SDL_failure_exit(const char *message) {
//     std::cerr << message << ", SDL_Error: " << SDL_GetError() << std::endl;
//     exit(EXIT_FAILURE);
// }

static constexpr uint16_t PORT = 52275;

struct Header {
    enum Type : asio::detail::u_short_type {
        CHAT,
    } type;
    asio::detail::u_long_type size;
};
static_assert(std::is_trivial_v<Header> && std::is_standard_layout_v<Header>);
constexpr size_t header_serialized_size =
    sizeof(asio::detail::u_short_type) + sizeof(asio::detail::u_long_type);

struct Chat {
    std::string message;
    std::string sender;
};

class Peer {
public:
    explicit Peer(asio::io_service &service)
        : service_{service}
        , resolver_{service_}
        , listener_(service_, {{}, PORT}) {
        listener_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        listener_.listen();
    }

    void
    accept_connections() {
        listener_.async_accept([&](asio::error_code const &ec, asio::ip::tcp::socket socket) {
            if (ec) {
                std::cerr << "[ERROR] " << ec.message() << std::endl;
            } else {
                std::cout << "New incoming connection from " << socket.remote_endpoint()
                          << std::endl;

                auto &peer = peers_.emplace_back(std::move(socket));
                receive_header(peer);
                accept_connections();
            }
        });
    }

    void
    connect(const std::string &host, const std::string &port) {
        auto  endpoint = resolver_.resolve(asio::ip::tcp::resolver::query(host, port));
        auto &peer     = peers_.emplace_back(asio::ip::tcp::socket(service_));
        asio::async_connect(
            peer.socket,
            endpoint,
            [&](const asio::error_code &ec, const asio::ip::tcp::endpoint &) {
                if (ec) {
                    std::cerr << "[ERROR] " << ec.message();
                } else {
                    std::cout << "Successfully connected to " << peer.socket.remote_endpoint()
                              << std::endl;
                    receive_header(peer);
                }
            });
    }

    void
    send(const asio::streambuf &buf) {
        if (peers_.empty()) {
            std::cout << "[WARN] Trying to send a message to 0 peers\n";
        }

        for (auto &peer : peers_) {
            peer.socket.async_send(buf.data(), [&](const asio::error_code &ec, size_t bytes) {
                if (ec) {
                    std::cerr << "[ERROR] " << ec.message();
                } else {
                    //                    std::cout << "[DEBUG] Sent " << bytes << " bytes to "
                    //                              << peer.socket.remote_endpoint() << std::endl;
                    (void)bytes;
                }
            });
        }
    }

    struct Socket {
        asio::ip::tcp::socket socket;
        asio::streambuf       buf{};

        explicit Socket(asio::ip::tcp::socket &&s)
            : socket{std::move(s)} {}
        ~Socket() = default;
    };

private:
    void
    receive_header(Socket &peer) {
        //        std::cout << "[DEBUG] Listening for messages from peer " <<
        //        peer.socket.remote_endpoint()
        //                  << std::endl;
        asio::async_read(
            peer.socket,
            peer.buf,
            asio::transfer_exactly(header_serialized_size),
            [&](const asio::error_code &ec, size_t bytes) {
                if (ec) {
                    std::cerr << "[WARN] " << ec.message() << std::endl;
                    std::cout << "Closing connection with " << peer.socket.remote_endpoint()
                              << std::endl;
                    peer.socket.close();
                    peers_.remove_if([p = &peer](auto &q) { return p == &q; });
                } else if (bytes != header_serialized_size) {
                    std::cerr << "[ERROR] Header is " << header_serialized_size << " bytes but "
                              << bytes << " were received";
                    std::cout << "Closing connection with " << peer.socket.remote_endpoint()
                              << std::endl;
                    peer.socket.close();
                    peers_.remove_if([p = &peer](auto &q) { return p == &q; });
                } else {
                    //                    std::cout << "[DEBUG] Received " << bytes << " bytes from
                    //                    "
                    //                              << peer.socket.remote_endpoint() << std::endl;

                    std::istream is(&peer.buf);

                    asio::detail::u_short_type type_nw;
                    asio::detail::u_long_type  size_nw;

                    is.read(reinterpret_cast<char *>(&type_nw), sizeof(type_nw));
                    is.read(reinterpret_cast<char *>(&size_nw), sizeof(size_nw));

                    auto type = asio::detail::socket_ops::network_to_host_short(type_nw);
                    auto size = asio::detail::socket_ops::network_to_host_long(size_nw);

                    (void)type;
                    receive_string(peer, size, [&](const std::string &str) {
                        std::cout << str << std::endl;
                    });
                }
            });
    }

    void
    receive_string(
        Socket                                         &peer,
        size_t                                          size,
        const std::function<void(const std::string &)> &callback) {
        //        std::cout << "[DEBUG] Waiting for peer " << peer.socket.remote_endpoint() << " to
        //        send "
        //                  << size << " bytes" << std::endl;
        asio::async_read(
            peer.socket,
            peer.buf,
            asio::transfer_exactly(size),
            [&, callback, size](const asio::error_code &ec, size_t bytes) {
                if (ec) {
                    std::cerr << "[WARN] " << ec.message() << std::endl;
                    std::cout << "Closing connection with " << peer.socket.remote_endpoint()
                              << std::endl;
                    peer.socket.close();
                    peers_.remove_if([p = &peer](auto &q) { return p == &q; });
                } else {
                    (void)bytes;
                    //                    std::cout << "[DEBUG] Received " << bytes << " bytes from
                    //                    "
                    //                              << peer.socket.remote_endpoint() << std::endl;
                    std::istream is(&peer.buf);
                    std::string  str(size, '\0');
                    is.read(str.data(), size);

                    receive_header(peer);
                    callback(str);
                }
            });
    }

    asio::io_service       &service_;
    asio::ip::tcp::resolver resolver_;
    std::list<Socket>       peers_;
    asio::ip::tcp::acceptor listener_;
};

struct Stun {
    const char *url;
    const char *port;
};

// clang-format off
static constexpr Stun public_stuns[]
    {{ "stun.l.google.com",     "19302", }
    ,{ "stun1.l.google.com",    "19302", }
    ,{ "stun2.l.google.com",    "19302", }
    ,{ "stun3.l.google.com",    "19302", }
    ,{ "stun4.l.google.com",    "19302", }
    ,{ "stun.stunprotocol.org", "3478",  }
    ,{ "stun.schlund.de",       "3478",  }
    ,{ "stun.voipbuster.com",   "3478",  }
    ,{ "stun.xten.com",         "3478",  }
    };
// clang-format on

std::ostream &
operator<<(std::ostream &os, const asio::ip::udp::endpoint &ep) {
    os << ep.address() << ":" << ep.port();
    return os;
}

// static asio::ip::udp::socket *sock_ptr = nullptr;

static std::mutex print_mut;

int
main(int argc, char **argv) try {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " target_ip\n";
        exit(EXIT_FAILURE);
    }
    std::string_view target_ip = argv[1];
    //    std::string_view local_port = (argc == 3) ? argv[2] : "0";

    using namespace std::literals;
    using udp = asio::ip::udp;

    auto ctx = asio::io_context();

    {
        auto endpoint = udp::endpoint(udp::v4(), 0);
        auto socket   = udp::socket(ctx);
        socket.open(udp::v4());
        socket.set_option(udp::socket::reuse_address(true));
        socket.bind(endpoint);

        //        sock_ptr = &socket;
        //        std::signal(SIGINT, [](int) {
        //            std::cout << "[DEBUG] caught SIGINT\n";
        //            if (sock_ptr) {
        //                std::cout << "[DEBUG] closing socket\n";
        //                asio::error_code ec;
        //                sock_ptr->close(ec);
        //                if (ec) {
        //                    std::cerr << ec.message() << std::endl;
        //                }
        //            }
        //            exit(EXIT_FAILURE);
        //        });

        asio::thread_pool thread_pool(8);

        std::vector<STUN::STUNRequest> requests;
        requests.reserve(std::size(public_stuns));

        std::transform(
            std::begin(public_stuns),
            std::end(public_stuns),
            std::back_inserter(requests),
            [&](const auto &ip_port) -> STUN::STUNRequest {
                auto &[stun_ip, stun_port] = ip_port;
                return {ctx, socket, stun_ip, stun_port};
            });

        std::mutex mut;
        for (auto &request : requests) {
            asio::post(thread_pool, [&] {
                request.run();
                do {
                    ctx.run_one();
                } while (!request.done());
            });
        }

        for (auto &request : requests) {
            auto res = request.result(1000ms);
            if (res) {
                std::unique_lock lock(mut);
                std::cout << res->ip << ":" << res->port << "\t[" << request.address() << "]\n";
            }
        }

        thread_pool.join();

        socket.close();
        //        sock_ptr = nullptr;
    }

    std::cout << "Enter target port, or nothing to guess port > ";
    std::string target_port;
    std::getline(std::cin, target_port);

    if (target_port.empty()) {
        udp::endpoint      send_ep;
        constexpr uint16_t min_port = 1024;
        constexpr uint16_t max_port = 65535;
        auto               resolver = udp::resolver(ctx);
        auto               ip       = std::string(target_ip);

        std::mutex data_mut;
        //        std::vector<uint16_t>                    data;
        //        std::vector<asio::const_buffer>          send_bufs;
        //        std::vector<udp::resolver::results_type> send_eps;
        std::vector<udp::endpoint> send_eps;
        send_eps.reserve(max_port - min_port + 1);

        for (int port = static_cast<int>(min_port); port <= static_cast<int>(max_port); port++) {
            send_eps.emplace_back(
                asio::ip::address::from_string(std::string(ip)),
                static_cast<asio::ip::port_type>(port));
        }

        asio::thread rec_thread([&ctx] {
            std::array<std::byte, 1024> buf{};
            udp::endpoint               rec_ep;
            auto                        socket = udp::socket(ctx);
            socket.open(udp::v4());
            socket.set_option(udp::socket::reuse_address(true));
            socket.bind(udp::endpoint(udp::v4(), 0));
            asio::error_code ec;
            auto             bytes = socket.receive_from(asio::buffer(buf), rec_ep, 0, ec);
            if (ec) {
                std::unique_lock lock(print_mut);
                std::cerr << ec.message() << std::endl;
                return;
            }
            {
                std::unique_lock lock(print_mut);
                std::cout << std::endl
                          << "received " << bytes << " bytes from " << rec_ep << std::endl;
                exit(EXIT_SUCCESS);
            }
        });

        while (true) {
            auto thread_pool = asio::thread_pool(8);

            auto socket = udp::socket(ctx);
            socket.open(udp::v4());
            socket.set_option(udp::socket::reuse_address(true));
            socket.bind(udp::endpoint(udp::v4(), 0));

            for (int port = static_cast<int>(min_port); port <= static_cast<int>(max_port);
                 port++) {
                auto port_str = std::to_string(port);

                socket.async_send_to(
                    asio::null_buffers(),
                    send_eps[port - min_port],
                    [port_str](const asio::error_code &ec, size_t) {
                        if (ec) {
                            std::unique_lock lock(print_mut);
                            std::cerr << port_str << " - " << ec.message() << std::endl;
                            return;
                        }
                        std::unique_lock lock(print_mut);
                        std::cout << port_str << " ";
                    });

                /*
                resolver.async_resolve(
                    udp::v4(),
                    ip,
                    port_str,
                    [&socket,
                     &data_mut,
                     &data,
                     &send_bufs,
                     &send_eps,
                     port,
                     str = (ip + ":" += port_str)](
                        const asio::error_code     &ec,
                        udp::resolver::results_type endpoints) {
                        if (ec) {
                            std::unique_lock lock(print_mut);
                            std::cerr << str << " - " << ec.message() << std::endl;
                            return;
                        }

                        data_mut.lock();
                        auto &d        = data.emplace_back(static_cast<uint16_t>(port));
                        auto &send_buf = send_bufs.emplace_back(asio::buffer(&d, 0));
                        auto &send_ep  = send_eps.emplace_back(std::move(endpoints));
                        data_mut.unlock();

                        socket.async_send_to(
                            send_buf,
                            *send_ep,
                            [str, &send_ep](const asio::error_code &ec, size_t bytes) {
                                (void)bytes;
                                if (ec) {
                                    std::unique_lock lock(print_mut);
                                    std::cerr << str << " - " << ec.message() << std::endl;
                                    return;
                                }
                                {
                                    std::unique_lock lock(print_mut);
                                    std::cout << send_ep->service_name() << " ";
                                }
                            });
                    });
                    */
                asio::post(thread_pool, [&] { ctx.run(); });
            }
            using namespace std::literals;
            thread_pool.wait();
            std::cout << "\ndone.\n";
        }
    } else {
        auto socket = udp::socket(ctx);
        socket.open(udp::v4());
        socket.set_option(udp::socket::reuse_address(true));
        socket.bind(udp::endpoint(udp::v4(), 0));

        auto endpoint = udp::endpoint(
            asio::ip::address::from_string(std::string(target_ip)),
            static_cast<asio::ip::port_type>(std::stoi(target_port)));

        asio::error_code ec;
        {
            auto bytes = socket.send_to(asio::null_buffers(), endpoint, 0, ec);
            if (ec) {
                std::cerr << ec.message() << std::endl;
                exit(EXIT_FAILURE);
            }
            std::cout << "sent " << bytes << " bytes to " << endpoint << std::endl;
        }
        {
            std::array<std::byte, 1024> buf{};
            udp::endpoint               rec_ep;
            auto bytes = socket.receive_from(asio::buffer(buf), rec_ep, 0, ec);
            if (ec) {
                std::cerr << ec.message() << std::endl;
                exit(EXIT_FAILURE);
            }
            {
                std::cout << std::endl
                          << "received " << bytes << " bytes from " << rec_ep << std::endl;
                exit(EXIT_SUCCESS);
            }
        }
    }
} catch (const std::exception &e) {
    std::cerr << "Exception thrown in main(): " << e.what() << std::endl;
    exit(EXIT_FAILURE);
}
