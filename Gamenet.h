#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using PlayerId = std::uint32_t;

// Причины отключения (уходят клиенту в пакете S2C_KICK)
constexpr std::uint8_t KICK_PROTOCOL = 1;   // нарушение протокола
constexpr std::uint8_t KICK_AUTH = 2;   // неверный токен
constexpr std::uint8_t KICK_BUSY = 3;   // сервер занят
constexpr std::uint8_t KICK_GAME = 4;   // решение игры: дубль входа, простой, нет мест
static constexpr std::uint8_t KICK_VERSION = 5;

// Игра вызывает это. Только из игрового потока.
// Реализация не должна вызывать IGameHandler синхронно из этих методов.
struct IGameNet {
    virtual ~IGameNet() = default;
    virtual void send(PlayerId id, const std::vector<std::uint8_t>& packet, bool reliable) = 0;
    virtual void disconnect(PlayerId id) = 0;
};

// Сеть вызывает это. Всегда в игровом потоке.
struct IGameHandler {
    virtual ~IGameHandler() = default;
    virtual void on_join(PlayerId id, int user_id, const std::string& name) = 0;   // токен проверен
    virtual void on_disconnect(PlayerId id) = 0;
    virtual void on_packet(PlayerId id, const std::uint8_t* data, std::size_t size) = 0;
};