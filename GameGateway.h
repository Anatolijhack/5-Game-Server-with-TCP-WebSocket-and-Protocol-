#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <boost/asio.hpp>
#include "ChatService.h"
#include "GameNet.h"
#include "Protocol.h"
#include "Session.h"

class GameGateway : public IGameNet {
public:
    GameGateway(boost::asio::io_context& game_io, ChatService& auth)
        : game_io_(game_io), auth_(auth) {}

    void set_game(IGameHandler& game) { game_ = &game; }

    // ===== вызывается из потока сессии =====
    void on_frame(const std::shared_ptr<Session>& s, std::string payload)
    {
        if (payload.empty()) return;

        if (s->game_id == 0)                       // ещё не вошёл: допустим один Hello с токеном
        {
            if (s->game_joining || static_cast<std::uint8_t>(payload[0]) != proto::C2S_HELLO)
            {
                s->raw_close(KICK_PROTOCOL);
                return;
            }
            s->game_joining = true;

            if (payload.size() < 2 ||
                static_cast<std::uint8_t>(payload[1]) != proto::PROTOCOL_VERSION)
            {
                s->raw_close(KICK_VERSION);
                return;
            }

            const std::string token = payload.substr(2, 128);
            const bool queued = auth_.verify_token(token,
                [this, s](std::optional<ChatService::TokenIdentity> id)      // рабочий поток
                {
                    s->ws_post([this, s, id]() { finish_join(s, id); });     // назад в поток сессии
                });

            if (!queued) s->raw_close(KICK_BUSY);
            return;
        }

        const PlayerId id = s->game_id;
        boost::asio::post(game_io_, [this, id, p = std::move(payload)]()
            {
                game_->on_packet(id, reinterpret_cast<const std::uint8_t*>(p.data()), p.size());
            });
    }

    // вызывается из ~Session (любой поток)
    void on_close(PlayerId id)
    {
        boost::asio::post(game_io_, [this, id]()
            {
                sessions_.erase(id);
                game_->on_disconnect(id);
            });
    }

    // ===== IGameNet: вызывается из игрового потока =====
    void send(PlayerId id, const std::vector<std::uint8_t>& packet, bool reliable) override
    {
        auto it = sessions_.find(id);
        if (it == sessions_.end()) return;
        auto s = it->second.lock();
        if (!s) return;

        std::string data(packet.begin(), packet.end());
        s->ws_post([s, data = std::move(data), reliable]() { s->send_raw(data, reliable); });
    }

    void disconnect(PlayerId id) override
    {
        auto it = sessions_.find(id);
        if (it == sessions_.end()) return;
        if (auto s = it->second.lock())
            s->ws_post([s]() { s->raw_close(KICK_GAME); });
    }

private:
    void finish_join(const std::shared_ptr<Session>& s,
        const std::optional<ChatService::TokenIdentity>& id)
    {
        if (!id) { s->raw_close(KICK_AUTH); return; }

        const PlayerId pid = next_id_++;
        s->game_id = pid;
        s->arm_timer(std::chrono::seconds(90));     // авторизован: обычный дедлайн простоя

        std::weak_ptr<Session> w = s;
        boost::asio::post(game_io_, [this, pid, w, uid = id->user_id, name = id->username]()
            {
                sessions_[pid] = w;                  // сначала регистрируем, потом приветствуем
                game_->on_join(pid, uid, name);
            });
    }

    boost::asio::io_context& game_io_;
    ChatService& auth_;
    IGameHandler* game_ = nullptr;
    std::atomic<PlayerId> next_id_{ 1 };
    std::unordered_map<PlayerId, std::weak_ptr<Session>> sessions_;   // только игровой поток
};