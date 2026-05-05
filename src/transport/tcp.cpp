#include "qjsb/transport/tcp.hpp"

namespace qjsb::transport {

TcpTransport::TcpTransport(boost::asio::io_context &io) : socket_(io) {}

TcpTransport::~TcpTransport() {
    close();
}

void TcpTransport::connect(std::string_view host, uint16_t port,
                           OnConnect on_connect, OnError on_error) {
    auto address = boost::asio::ip::make_address(std::string(host));
    boost::asio::ip::tcp::endpoint ep(address, port);

    if (!socket_.is_open())
        socket_.open(boost::asio::ip::tcp::v4());

    socket_.async_connect(ep, [this, on_connect = std::move(on_connect),
                               on_error = std::move(on_error)](
                                  const boost::system::error_code &ec) {
        if (ec) {
            on_error(ec.message());
            return;
        }
        connected_ = true;
        on_connect();
        if (reading_)
            doRead();
    });
}

void TcpTransport::startRead(OnData on_data, OnError on_error, OnClose on_close) {
    on_data_ = std::move(on_data);
    on_error_ = std::move(on_error);
    on_close_ = std::move(on_close);
    reading_ = true;
    if (connected_)
        doRead();
}

void TcpTransport::stopRead() { reading_ = false; }

void TcpTransport::doRead() {
    if (!reading_ || !connected_)
        return;

    socket_.async_read_some(
        boost::asio::buffer(read_buf_),
        [this](const boost::system::error_code &ec, size_t n) {
            if (ec == boost::asio::error::operation_aborted) {
                reading_ = false;
                if (on_close_)
                    on_close_();
                return;
            }

            if (ec) {
                reading_ = false;
                if (on_error_)
                    on_error_(ec.message());
                if (on_close_)
                    on_close_();
                return;
            }

            if (on_data_)
                on_data_(std::span<const uint8_t>(read_buf_.data(), n));

            if (reading_ && connected_)
                doRead();
        });
}

void TcpTransport::write(std::vector<uint8_t> data, OnWriteComplete on_complete,
                         OnError on_error) {
    auto buf = std::make_shared<std::vector<uint8_t>>(std::move(data));
    boost::asio::async_write(
        socket_, boost::asio::buffer(*buf),
        [this, buf, on_complete = std::move(on_complete),
         on_error = std::move(on_error)](const boost::system::error_code &ec,
                                         size_t /*n*/) {
            if (ec) {
                if (on_error_)
                    on_error_(ec.message());
                else if (on_error)
                    on_error(ec.message());
                return;
            }
            if (on_complete)
                on_complete();
        });
}

void TcpTransport::close() noexcept {
    boost::system::error_code ec;
    if (socket_.is_open())
        socket_.close(ec);
    connected_ = false;
    reading_ = false;
}

void TcpTransport::cancel() noexcept {
    boost::system::error_code ec;
    if (socket_.is_open())
        socket_.cancel(ec);
}

bool TcpTransport::isConnected() const noexcept { return connected_; }

bool TcpTransport::isOpen() const noexcept { return socket_.is_open(); }

} // namespace qjsb::transport
