#pragma once
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <unordered_map>
#include "ThreadPool.h"
#include "Router.h"
#include <optional>
#include <queue>
#include "GameNet.h"
using namespace std::chrono_literals;
using boost::asio::ip::tcp;
class GameGateway;

class ChatService;
class Session : public std::enable_shared_from_this<Session>
{
private:
	enum class ParseState
	{
		RequestLine,
		Header,
		Body
	};
	enum class ParseResult
	{
		Incomplete,
		Complete,
		Error
	};
	static constexpr size_t MAX_WRITE_QUEUE = 256;
	void do_shutdown();
	std::string websocket_fragment_buffer;
	bool websocket_fragmented = false;
	ParseState state = ParseState::RequestLine;
	std::string path;
	std::string version;
	std::string method;
	std::unordered_map<std::string, std::string> headers;
	std::array<char, 4096> temp;
	int content_lenght = 0;
	void enqueue_ws_frame(std::shared_ptr<std::string> frame);
	bool is_valid_utf8(
		const std::string& data);	std::deque<std::shared_ptr<std::string>> write_queue;
	bool writing = false;
	bool websocket_close_sent = false;
	bool websocket_close_received = false;
	boost::asio::ssl::stream<tcp::socket> socket;
	std::string buffer;
	Router& router;
	std::string body;
	bool keep_alive = true;
	int content_length = 0;
	static constexpr std::size_t MAX_REQUEST_SIZE = 10 * 1024 * 1024;
	ThreadPool& pool;
	void do_write();
	void send_websocket_close();
	void do_read();
	/*bool parse();*/
	ParseResult parse();
	void process_request();
	void reset_parser();
	void send_response(const std::string& body, const std::string& type,
		const std::string& status, const HeaderList& extra = {});
	void send_response_safe(const std::string& body, const std::string& type,
		const std::string& status, const HeaderList& extra = {});
	bool websocket_mode = false;
	bool websocket_fragment_binary = false;

	bool is_websocket_request() const;
	void do_websocket_handshake();
	void do_websocket_read();
	void send_websocket_text(const std::string& message);
	void send_websocket_binary(const std::string& data);
	void send_websocket_pong(const std::string& payload);
	bool is_valid_websocket_close_code(
		std::uint16_t code);
	void send_websocket_close(
		std::uint16_t code,
		const std::string& reason = ""
	);
	void do_sniff();
	bool shutting_down = false;

	void websocket_protocol_error(
		std::uint16_t code,
		const std::string& reason
	);
	ChatService* chat = nullptr;
	std::chrono::steady_clock::time_point rate_window_start{};
	int rate_count = 0;
	boost::asio::steady_timer idle_timer;
	boost::asio::steady_timer write_timer;
	boost::asio::steady_timer ping_timer;
	std::uint64_t idle_gen = 0;
	std::uint64_t write_gen = 0;
	bool request_in_progress = false;
	bool ws_read_started = false;


	void cancel_timers();
	void force_close();
	void arm_write_watchdog();
	void start_ping_loop();
	void send_websocket_ping();
	// ---- SSE ----


public:
	bool raw_mode = false;
	bool raw_closing = false;
	std::array<unsigned char, 4> raw_hdr{};
	std::string raw_body;
	GameGateway* gateway = nullptr;
	std::uint32_t game_id = 0;           // 0: в игру ещё не вошёл
	bool game_joining = false;
	void raw_begin();
	void do_raw_read();
	void do_raw_read_body();
	void send_raw(const std::string& data, bool reliable = true);
	void send_raw_heartbeat();
	void raw_close(std::uint8_t reason);
	void arm_timer(std::chrono::seconds t);
	// --- SSE---;
	bool sse_mode = false;
	bool sse_reading = false;
	std::string stream_id;
	bool handle_complete_request();
	void handle_sse();
	void handle_chat_post();
	void sse_begin();
	void reject_http(const std::string& status);
	void send_sse(const std::string& json);
	void send_sse_comment();
	void do_sse_read();
	//--- Long Pool----;
	bool poll_mode = false;
	std::string device_id;
	std::uint64_t poll_seq = 0;
	std::deque<std::pair<std::uint64_t, std::string>> poll_outbox;
	std::weak_ptr<Session> poll_waiter;
	boost::asio::steady_timer poll_timer;
	boost::asio::ssl::context& ssl_ctx;

	static std::shared_ptr<Session> make_device(Session& from);
	void handle_poll();
	void poll_push(const std::string& json);
	void poll_take(const std::shared_ptr<Session>& waiter, std::uint64_t ack);
	std::string poll_json() const;
	static void poll_reply(const std::shared_ptr<Session>& waiter, std::string json);
	void device_expire();
	struct ChatIdentity { int user_id; std::string login; std::string role; };
	std::optional<ChatIdentity> chat_identity;
	void ws_post(std::function<void()> f)
	{
		boost::asio::post(socket.get_executor(),
			[self = shared_from_this(), f = std::move(f)]() { f(); });
	}
	void ws_send(std::string text)
	{
		ws_post([this, text = std::move(text)]()
			{
				ws_post([this, text = std::move(text)]()
					{
						if (raw_mode)       send_raw(text);
						else if (sse_mode)  send_sse(text);
						else if (poll_mode) poll_push(text);
						else                send_websocket_text(text);
					});
			});
	}
	void ws_close(std::uint16_t code, const std::string& reason)
	{
		ws_post([this, code, reason]()
			{
				if (raw_mode)       raw_close(code);
				else if (sse_mode)  do_shutdown();
				else if (poll_mode) device_expire();
				else                send_websocket_close(code, reason);
			});
	}
	bool allow_chat_message();    // 10 сообщений за 10 секунд

	Session(tcp::socket socket, ThreadPool& pool, Router& router,
		boost::asio::ssl::context& ssl_context, ChatService* chat);
	~Session();
	void start();
};