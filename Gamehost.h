#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include "GameNet.h"
#include "PartyManager.h"
#include "Protocol.h"

// Серверный слой: сеть и маршрутизация. Правила пати живут в Party и PartyManager.
// Работает в игровом потоке, про сокеты ничего не знает.

constexpr std::uint32_t IDLE_TIMEOUT = 10 * TICK_RATE;
constexpr std::size_t   MAX_INPUT_QUEUE = 16;
constexpr std::size_t   MAX_CONNECTIONS = 64;               // защита от флуда
constexpr int           MAX_FAILS = 5;                      // неудачных входов
constexpr std::uint32_t FAIL_WINDOW = 60 * TICK_RATE;
static constexpr int MAX_CMDS_PER_SEC = 10;// за минуту

struct InputCmd
{
    std::uint32_t seq = 0;
    std::uint8_t  flags = 0;
    std::uint8_t  nums = 0;
    std::int16_t  aimX = 0;
    std::int16_t  aimY = 0;
};

enum class ConnState { Menu, InParty };

struct Slot
{
    int           user_id = 0;
    std::string   name;
    ConnState     state = ConnState::Menu;
    std::deque<InputCmd> inputs;
    std::uint32_t last_seq = 0;
    std::uint32_t last_rx_tick = 0;
    bool          kicked = false;
    std::uint32_t fail_window_start = 0;
    int           fails = 0;
    std::uint32_t cmd_window_start = 0;
    int           cmd_count = 0;
};

class GameHost : public IGameHandler
{
public:
    std::uint32_t tick_ = 0;
    std::unordered_map<PlayerId, Slot> slots_;

    explicit GameHost(IGameNet& net) : net_(net) {}

    // ===== вызовы от сети =====

    void on_join(PlayerId id, int user_id, const std::string& name) override
    {
        std::cout << "on_join id=" << id << " user=" << user_id
            << " slots=" << slots_.size() << std::endl;
        if (slots_.size() >= MAX_CONNECTIONS || user_in_game(user_id))
        {
            net_.disconnect(id);
            return;
        }

        Slot s;
        s.user_id = user_id;
        s.name = name;
        s.last_rx_tick = tick_;
        s.fail_window_start = tick_;
        slots_[id] = std::move(s);

        // Welcome теперь значит только «вы вошли», мира у игрока ещё нет
        proto::Writer w;
        w.u8(proto::S2C_WELCOME);
        w.u8(proto::PROTOCOL_VERSION);
        w.u32(id);
        w.u8(static_cast<std::uint8_t>(TICK_RATE));
        w.u16(static_cast<std::uint16_t>(ARENA_W));
        w.u16(static_cast<std::uint16_t>(ARENA_H));
        net_.send(id, w.b, true);
    }

    void on_disconnect(PlayerId id) override
    {
        std::cout << "on_disconnect id=" << id << std::endl;
        leave_party(id, false, 0);
        slots_.erase(id);
    }

    void on_packet(PlayerId id, const std::uint8_t* data, std::size_t size) override
    {
        auto it = slots_.find(id);
        if (it == slots_.end() || size == 0) return;

        Slot& s = it->second;
        s.last_rx_tick = tick_;

        proto::Reader r(data, size);
        std::uint8_t type = 0;
        r.u8(type);

        switch (type)
        {
        case proto::C2S_INPUT:
        {
            InputCmd c;
            if (!r.u32(c.seq) || !r.u8(c.flags) || !r.u8(c.nums) || !r.i16(c.aimX) || !r.i16(c.aimY)
                || (c.flags & 0xC0) || (c.nums & 0xE0))
            {
                kick(id, s);
                return;
            }
            if (s.state != ConnState::InParty) break;     // в меню ввод игнорируем
            if (s.inputs.size() >= MAX_INPUT_QUEUE) s.inputs.pop_front();
            s.inputs.push_back(c);
            break;
        }

        case proto::C2S_PING:
        {
            proto::Writer w;
            w.u8(proto::S2C_PONG);
            net_.send(id, w.b, false);
            break;
        }

        case proto::C2S_CREATE_PARTY:
        {
            if (r.left() != 0) { kick(id, s); return; }
            if (s.state == ConnState::InParty) { send_error(id, proto::ERR_ALREADY_IN); break; }

            PErr err;
            if (!allow_cmd(s)) { send_error(id, proto::ERR_RATE); break; }
            Party* p = parties_.create(id, tick_, s.user_id, s.name, err);
            if (!p) { send_error(id, to_code(err)); break; }

            s.state = ConnState::InParty;
            s.inputs.clear();
            send_party_joined(id, *p);
            broadcast_state(*p);
            break;
        }

        case proto::C2S_JOIN_PARTY:
        {
            std::string code;
            if (!proto::decode_join_party(data, size, code)) { kick(id, s); return; }

            if (s.state == ConnState::InParty) { send_error(id, proto::ERR_ALREADY_IN); break; }
            if (!allow_attempt(s)) { send_error(id, proto::ERR_RATE); break; }

            PErr err;
            Party* p = parties_.join(id, code, tick_, s.user_id, s.name, err);
            if (!p)
            {
                ++s.fails;
                send_error(id, to_code(err));
                break;
            }

            s.state = ConnState::InParty;
            s.inputs.clear();
            send_party_joined(id, *p);
            broadcast_state(*p);
            break;
        }

        case proto::C2S_LEAVE_PARTY:
        {
            if (r.left() != 0) { kick(id, s); return; }
            if (s.state != ConnState::InParty) { send_error(id, proto::ERR_NOT_IN); break; }
            leave_party(id, true, proto::LEFT_BY_USER);
            break;
        }

        case proto::C2S_SET_READY:
        {
            bool ready = false;
            if (!proto::decode_set_ready(data, size, ready)) { kick(id, s); return; }

            Party* p = parties_.of(id);
            if (s.state != ConnState::InParty || !p) { send_error(id, proto::ERR_NOT_IN); break; }
            if (!allow_cmd(s)) break;
            if (p->set_ready(id, ready) == PRes::Ok) broadcast_state(*p);
            break;
        }

        case proto::C2S_START:
        {
            if (r.left() != 0) { kick(id, s); return; }
            Party* p = parties_.of(id);
            if (s.state != ConnState::InParty || !p) { send_error(id, proto::ERR_NOT_IN); break; }
            if (!allow_cmd(s)) break;
            switch (p->begin_countdown(id))
            {
            case PRes::Ok:        broadcast_state(*p); break;
            case PRes::NotLeader: send_error(id, proto::ERR_NOT_LEADER); break;
            case PRes::NotReady:  send_error(id, proto::ERR_NOT_READY); break;
            default: break;       // неподходящее состояние: игнорируем
            }
            break;
        }

        case proto::C2S_KICK:
        {
            std::uint8_t num = 0;
            if (!proto::decode_kick_member(data, size, num)) { kick(id, s); return; }

            Party* p = parties_.of(id);
            if (s.state != ConnState::InParty || !p) { send_error(id, proto::ERR_NOT_IN); break; }
            if (!allow_cmd(s)) break;

            PlayerId target = 0;
            switch (p->kick(id, num, target))
            {
            case PRes::Ok:        leave_party(target, true, proto::LEFT_KICKED); break;
            case PRes::NotLeader: send_error(id, proto::ERR_NOT_LEADER); break;
            case PRes::BadTarget: send_error(id, proto::ERR_BAD_TARGET); break;
            default: break;
            }
            break;
        }

        default:
            kick(id, s);                                   // неизвестный тип, в том числе повторный Hello
        }
    }

    // ===== один тик =====

    void tick()
    {
        ++tick_;

        // 1. Таймауты и ввод
        for (auto& [id, s] : slots_)
        {
            if (s.state != ConnState::InParty)
            {
                if (!s.kicked && tick_ - s.last_rx_tick > IDLE_TIMEOUT) kick(id, s);
                continue;
            }

            Party* p = parties_.of(id);
            if (!p) continue;
            const std::uint8_t num = p->num_of(id);

            if (s.kicked)
            {
                p->set_input(num, InputState{});          // кикнутый не бегает по старому вводу
                continue;
            }
            if (tick_ - s.last_rx_tick > IDLE_TIMEOUT)
            {
                kick(id, s);
                continue;
            }
            p->set_input(num, take_input(s));
        }

        // 2. Каждая пати тикает один раз и рассылает снимки своим участникам
        parties_.for_each([&](Party& p)
            {
                switch (p.tick())
                {
                case PEvent::Started: broadcast_started(p); broadcast_state(p); break;
                case PEvent::Ended:   broadcast_ended(p);   broadcast_state(p); break;
                case PEvent::Changed:
                case PEvent::ToLobby: broadcast_state(p); break;
                default: break;
                }

                if (p.state() != PartyState::Playing) return;   // снимки только в игре

                proto::Writer world;
                p.write_world(world);

                for (const Member& m : p.members())
                {
                    auto it = slots_.find(m.conn);
                    if (it == slots_.end() || it->second.kicked) continue;

                    proto::Writer w;
                    w.u8(proto::S2C_SNAPSHOT);
                    w.u32(tick_);
                    w.u32(it->second.last_seq);
                    w.bytes(world.b);
                    net_.send(m.conn, w.b, false);
                }
            });

        if (tick_ % (10 * TICK_RATE) == 0) parties_.sweep();
    }

    void broadcast_state(const Party& p)
    {
        proto::PartyUpdateMsg m;
        m.state = static_cast<std::uint8_t>(p.state());
        m.countdown = p.countdown_sec();
        m.leader = p.leader();
        for (const Member& mem : p.members())
            m.members.push_back({ mem.num, mem.num == p.leader() || mem.ready, mem.name });

        proto::Writer w;
        proto::encode(w, m);
        for (const Member& mem : p.members()) net_.send(mem.conn, w.b, true);
    }

    void broadcast_started(const Party& p)
    {
        proto::Writer w;
        w.u8(proto::S2C_GAME_STARTED);
        for (const Member& m : p.members()) net_.send(m.conn, w.b, true);
    }

    void broadcast_ended(const Party& p)
    {
        const MatchResults& r = p.results();

        proto::GameEndedMsg m;
        m.wave = r.wave;
        m.score = r.score;
        m.points = r.points;          // тот же тип: vector<pair<u8, u16>>

        proto::Writer w;
        proto::encode(w, m);
        for (const Member& mem : p.members()) net_.send(mem.conn, w.b, true);
    }

private:
    // ---------- пати ----------


  
     
    void leave_party(PlayerId id, bool notifySelf, std::uint8_t reason)
    {
        bool destroyed = false;
        Party* p = parties_.leave(id, destroyed);
        if (p) send_party_update(*p, 0);                  // остальным: состав и лидер

        auto it = slots_.find(id);
        if (it != slots_.end())
        {
            it->second.state = ConnState::Menu;
            it->second.inputs.clear();
        }
        if (notifySelf)
        {
            proto::Writer w;
            w.u8(proto::S2C_PARTY_LEFT);
            w.u8(reason);
            net_.send(id, w.b, true);
        }
    }

    void send_party_joined(PlayerId id, const Party& p)
    {
        proto::PartyJoinedMsg m;
        m.num = p.num_of(id);
        m.code = p.code();
        proto::Writer w;
        proto::encode(w, m);
        net_.send(id, w.b, true);
    }
    // except: кому не слать (0 = слать всем)
    void send_party_update(const Party& p, PlayerId except)
    {
        proto::Writer w;
        w.u8(proto::S2C_PARTY_UPDATE);
        w.u8(p.leader());
        w.u8(static_cast<std::uint8_t>(p.members().size()));
        for (const Member& m : p.members()) w.u8(m.num);

        for (const Member& m : p.members())
            if (m.conn != except) net_.send(m.conn, w.b, true);
    }

    void send_error(PlayerId id, std::uint8_t code)
    {
        proto::Writer w;
        w.u8(proto::S2C_ERROR);
        w.u8(code);
        net_.send(id, w.b, true);
    }

    static std::uint8_t to_code(PErr e)
    {
        switch (e)
        {
        case PErr::AlreadyIn: return proto::ERR_ALREADY_IN;
        case PErr::Limit:     return proto::ERR_LIMIT;
        default:              return proto::ERR_NO_PARTY;   // NotFound и Full не различаем
        }
    }

    static bool read_code(proto::Reader& r, std::string& code)
    {
        char buf[proto::PARTY_CODE_LEN];
        if (!r.bytes(reinterpret_cast<std::uint8_t*>(buf), sizeof buf) || r.left() != 0)
            return false;
        code.assign(buf, sizeof buf);
        return true;
    }

    bool allow_attempt(Slot& s)
    {
        if (tick_ - s.fail_window_start > FAIL_WINDOW) { s.fail_window_start = tick_; s.fails = 0; }
        return s.fails < MAX_FAILS;
    }

    bool allow_cmd(Slot& s)
    {
        if (tick_ - s.cmd_window_start >= TICK_RATE) { s.cmd_window_start = tick_; s.cmd_count = 0; }
        return ++s.cmd_count <= MAX_CMDS_PER_SEC;
    }

    // ---------- ввод и соединения ----------

    InputState take_input(Slot& s)
    {
        InputState in{};
        if (s.inputs.empty()) return in;

        const InputCmd c = s.inputs.front();
        s.inputs.pop_front();

        in.left = (c.flags & proto::IN_LEFT) != 0;
        in.right = (c.flags & proto::IN_RIGHT) != 0;
        in.up = (c.flags & proto::IN_UP) != 0;
        in.down = (c.flags & proto::IN_DOWN) != 0;
        in.shoot = (c.flags & proto::IN_SHOOT) != 0;
        in.restart = (c.flags & proto::IN_RESTART) != 0;
        in.aimX = static_cast<float>(c.aimX);
        in.aimY = static_cast<float>(c.aimY);
        in.num1 = (c.nums & 1) != 0;
        in.num2 = (c.nums & 2) != 0;
        in.num3 = (c.nums & 4) != 0;
        in.num4 = (c.nums & 8) != 0;
        in.num5 = (c.nums & 16) != 0;

        s.last_seq = c.seq;
        return in;
    }

    void kick(PlayerId id, Slot& s)
    {
        if (s.kicked) return;
        s.kicked = true;
        net_.disconnect(id);             // отложенно: on_disconnect придёт позже
    }

    bool user_in_game(int user_id) const
    {
        for (const auto& kv : slots_)
            if (kv.second.user_id == user_id) return true;
        return false;
    }

    IGameNet& net_;
    PartyManager parties_;
    
};