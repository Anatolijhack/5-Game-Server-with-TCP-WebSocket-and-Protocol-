#include "Server.h"
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>
#include "Logger.h"
#include "SessionRepository.h"
#include "ChatService.h"
#include "Сonfig.h"
#include "AuthControler.h"
#include "SessionCleaner.h"
#include "Game.h"
#include "GameGateway.h"
#include "TickStats.h"

// Это подключаем ПОСЛЕ Boost.Asio (иначе конфликт winsock2.h и windows.h)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif
#include "Gamehost.h"

namespace asio = boost::asio;

int main(int argc, char** argv)
{
    Logger::instance().init("server.log");
    LOG_INFO("Server starting...");

#ifdef _WIN32
    timeBeginPeriod(1);     // таймеры с точностью ~1 мс (иначе тик "дрожит" на 15 мс)
#endif

    Config config = Config::load("config.json");
    Router router;

    ConnectionPool db_pool(
        config.database.pool_size,
        config.database.to_connection_string()
    );
    SessionRepository session_repository(db_pool, std::chrono::minutes(30), std::chrono::hours(12));
    SessionCleaner session_cleaner(session_repository);

    ThreadPool pool(4);
    ChatService chat(
        db_pool,
        session_repository,
        pool,
        { "https://localhost:8080/" });

    AuthController auth_controller(db_pool, session_repository, chat);
    auth_controller.register_routes(router);

    // ---------- игровая часть ----------
    // Объявлена ДО io и server: деструкторы сессий обращаются к gateway и game_io,
    // поэтому они должны жить дольше сессий.
    asio::io_context game_io;
    GameGateway gateway(game_io, chat);
    GameHost game(gateway);
    gateway.set_game(game);

    TickStats stats(1000.0 / TICK_RATE);
    asio::steady_timer game_timer(game_io);
    auto next = std::chrono::steady_clock::now();
    const auto period = std::chrono::nanoseconds(1'000'000'000 / TICK_RATE);

    std::function<void()> loop = [&]() {
        next += period;

        // Сильно отстали: не догоняем пачкой тиков, а пропускаем накопившееся время
        if (std::chrono::steady_clock::now() - next > period * 5) {
            next = std::chrono::steady_clock::now();
            stats.skipped();
        }

        game_timer.expires_at(next);
        game_timer.async_wait([&](boost::system::error_code ec) {
            if (ec) return;

            const auto t0 = std::chrono::steady_clock::now();
            const double late = std::chrono::duration<double, std::milli>(t0 - next).count();

            game.tick();

            const double dur = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
            stats.add(dur, late);

            loop();
            });
        };
    loop();

    std::thread game_thread([&]() {
        try { game_io.run(); }
        catch (const std::exception& e) { LOG_ERROR(std::string("Game thread: ") + e.what()); }
        });

    // ---------- сетевая часть ----------
    try
    {
        asio::io_context io;

        Server server(io, 8765, pool, router, chat);   // второй порт (TCP-кадры): 8766 по умолчанию
        server.set_gateway(&gateway);                   // до io.run(): приём подключений ещё не пошёл

        std::cout << "Server started: 8765 (HTTP/WebSocket/TCP), 8766 (TCP frames)\n";

        io.run();
        /*pool.shutdown();*/
    }
    catch (std::exception& e)
    {
        std::cout << "Error: " << e.what() << std::endl;
    }

    // Остановка игры: сначала поток, потом (при выходе из main) объекты игры
    game_io.stop();
    game_thread.join();
}