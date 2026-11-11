#pragma once
#include "Session.h"
#include <boost/asio/ssl.hpp>
#include "Router.h"
class GameGateway;
class Server
{
private:
	
	void accept_raw();
	void stop();
	tcp::acceptor raw_acceptor;
	tcp::acceptor acceptor;
	boost::asio::steady_timer accept_timer;        // новое
	boost::asio::steady_timer raw_accept_timer;    // новое
	ThreadPool& pool;
	Router& router;
	ChatService& chat;
	boost::asio::ssl::context ssl_context;
	GameGateway* gateway_ = nullptr;
public:
	void set_gateway(GameGateway* g) { gateway_ = g; }
	Server(boost::asio::io_context& io, short port, ThreadPool& pool,
		Router& router, ChatService& chat, short raw_port = 8766);
	void accept();
};