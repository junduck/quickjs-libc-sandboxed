#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>

namespace qjsb::transport {

using OnConnect = std::function<void()>;
using OnData = std::function<void(std::span<const uint8_t>)>;
using OnError = std::function<void(std::string_view)>;
using OnClose = std::function<void()>;
using OnWriteComplete = std::function<void()>;

class TcpTransport {
public:
    explicit TcpTransport(boost::asio::io_context &io);
    ~TcpTransport();

    TcpTransport(const TcpTransport &) = delete;
    TcpTransport &operator=(const TcpTransport &) = delete;

    void connect(std::string_view host, uint16_t port, OnConnect on_connect, OnError on_error);

    void startRead(OnData on_data, OnError on_error, OnClose on_close);
    void stopRead();

    void write(std::vector<uint8_t> data, OnWriteComplete on_complete, OnError on_error);

    void close() noexcept;
    void cancel() noexcept;
    bool isConnected() const noexcept;
    bool isOpen() const noexcept;

private:
    boost::asio::ip::tcp::socket socket_;
    std::array<uint8_t, 4096> read_buf_{};
    bool reading_ = false;
    bool connected_ = false;

    OnData on_data_;
    OnError on_error_;
    OnClose on_close_;

    void doRead();
};

} // namespace qjsb::transport
