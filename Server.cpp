#include "Server.h"
#include <iostream>
static bool is_transient_accept_error(const boost::system::error_code& ec)
{
	return ec == boost::asio::error::connection_aborted ||
		ec == boost::asio::error::connection_reset ||
		ec == boost::asio::error::interrupted;
}
Server::Server(boost::asio::io_context& io, short port, ThreadPool& pool,
	Router& router, ChatService& chat, short raw_port)
	: acceptor(io, tcp::endpoint(tcp::v4(), port)),
	raw_acceptor(io, tcp::endpoint(tcp::v4(), raw_port)),
	accept_timer(io),
	raw_accept_timer(io),
	pool(pool), router(router),
	chat(chat),
	ssl_context(boost::asio::ssl::context::tlsv12_server)
{
	ssl_context.use_certificate_chain_file("D:/Мои проект/Http Server for Order/x64/Release/server.crt");
	ssl_context.use_private_key_file("D:/Мои проект/Http Server for Order/x64/Release/server.key", boost::asio::ssl::context::pem);

	accept();
	accept_raw();
}

void Server::accept()
{
	acceptor.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
		if (ec == boost::asio::error::operation_aborted)
			return;                                    // сервер останавливается

		if (!ec) {
			auto s = std::make_shared<Session>(std::move(socket), pool, router, ssl_context, &chat);
			s->gateway = gateway_;                     // для игровых кадров, если клиент придёт сюда
			s->start();                                // raw_mode НЕ ставим: протокол определит do_sniff()
			accept();
			return;
		}

		std::cerr << "Accept error: " << ec.message() << std::endl;

		if (is_transient_accept_error(ec)) {
			accept();
			return;
		}

		// серьёзная ошибка (нет дескрипторов, нет памяти): пауза, чтобы не крутить цикл
		accept_timer.expires_after(std::chrono::milliseconds(200));
		accept_timer.async_wait([this](boost::system::error_code e) {
			if (!e) accept();
			});
		});
}

void Server::accept_raw()
{
	raw_acceptor.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
		if (ec == boost::asio::error::operation_aborted)
			return;

		if (!ec) {
			auto s = std::make_shared<Session>(std::move(socket), pool, router, ssl_context, &chat);
			s->gateway = gateway_;
			s->raw_mode = true;                        // этот порт только для TCP-кадров
			s->start();
			accept_raw();
			return;
		}

		std::cerr << "Raw accept error: " << ec.message() << std::endl;

		if (is_transient_accept_error(ec)) {
			accept_raw();
			return;
		}

		raw_accept_timer.expires_after(std::chrono::milliseconds(200));
		raw_accept_timer.async_wait([this](boost::system::error_code e) {
			if (!e) accept_raw();
			});
		});
}
void Server::stop()
{
	boost::system::error_code ec;
	acceptor.close(ec);
	raw_acceptor.close(ec);
	accept_timer.cancel();
	raw_accept_timer.cancel();
}
