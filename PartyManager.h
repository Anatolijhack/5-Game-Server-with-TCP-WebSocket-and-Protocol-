// PartyManager.h
#pragma once
#include <openssl/rand.h>
#include <cctype>
#include <string>
#include <unordered_map>
#include "Party.h"

constexpr std::size_t MAX_PARTIES = 8;
constexpr std::size_t CODE_LEN = 6;

enum class PErr { None, NotFound, Full, AlreadyIn, Limit };

class PartyManager
{
public:
    Party* of(PlayerId conn)
    {
        auto it = by_conn_.find(conn);
        return it == by_conn_.end() ? nullptr : it->second;
    }

    Party* create(PlayerId conn, std::uint32_t tick, int user_id, const std::string& name, PErr& err)
    {
        err = PErr::None;
        if (of(conn)) { err = PErr::AlreadyIn; return nullptr; }
        if (by_code_.size() >= MAX_PARTIES) { err = PErr::Limit;     return nullptr; }

        std::string code = gen_code();
        if (code.empty()) { err = PErr::Limit; return nullptr; }

        auto p = std::make_unique<Party>(code);
        Party* raw = p.get();
        if (!raw->add_member(conn, tick, user_id, name)) { err = PErr::Limit; return nullptr; }

        by_code_[code] = std::move(p);
        by_conn_[conn] = raw;               // оба индекса обновляются в одной функции
        return raw;
    }

    Party* join(PlayerId conn, std::string raw_code, std::uint32_t tick,
        int user_id, const std::string& name, PErr& err)
    {
        err = PErr::None;
        if (of(conn)) { err = PErr::AlreadyIn; return nullptr; }

        if (!normalize(raw_code)) { err = PErr::NotFound; return nullptr; }   // формат неверный
        auto it = by_code_.find(raw_code);
        if (it == by_code_.end()) { err = PErr::NotFound; return nullptr; }

        Party* p = it->second.get();
        if (!p->add_member(conn, tick, user_id, name)) { err = PErr::NotFound; return nullptr; }
        {
            err = PErr::NotFound;           // «заполнена» и «нет такой» наружу не различаем
            return nullptr;
        }
        by_conn_[conn] = p;
        return p;
    }

    // Возвращает пати, из которой вышли (nullptr если соединение не было в пати).
    // Если пати опустела, она уже уничтожена, поэтому указатель после этого использовать нельзя:
    // в этом случае возвращается nullptr и destroyed = true.
    Party* leave(PlayerId conn, bool& destroyed)
    {
        destroyed = false;
        auto it = by_conn_.find(conn);
        if (it == by_conn_.end()) return nullptr;

        Party* p = it->second;
        p->remove_member(conn);
        by_conn_.erase(it);

        if (p->empty())
        {
            const std::string code = p->code();     // копия: сам Party сейчас будет уничтожен
            by_code_.erase(code);
            destroyed = true;
            return nullptr;
        }
        return p;
    }

    template <class F> void for_each(F&& f)
    {
        for (auto& kv : by_code_) f(*kv.second);
    }

    // На случай ошибки учёта: пустые пати не живут
    void sweep()
    {
        for (auto it = by_code_.begin(); it != by_code_.end();)
            it = it->second->empty() ? by_code_.erase(it) : std::next(it);
    }

private:
    static constexpr const char* ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";   // 31 знак
    static constexpr int ALPHA_N = 31;

    static bool normalize(std::string& s)
    {
        if (s.size() != CODE_LEN) return false;
        for (char& c : s)
        {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (!std::strchr(ALPHABET, c) || c == '\0') return false;
        }
        return true;
    }

    std::string gen_code()
    {
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            std::string code;
            while (code.size() < CODE_LEN)
            {
                unsigned char b;
                if (RAND_bytes(&b, 1) != 1) return {};
                if (b >= 248) continue;               // 248 = 31*8: без смещения при %
                code.push_back(ALPHABET[b % ALPHA_N]);
            }
            if (!by_code_.count(code)) return code;   // уникальность среди живых
        }
        return {};
    }

    std::unordered_map<std::string, std::unique_ptr<Party>> by_code_;
    std::unordered_map<PlayerId, Party*> by_conn_;
};