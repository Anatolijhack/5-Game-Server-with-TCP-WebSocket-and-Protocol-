#pragma once
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "Instance.h"

constexpr int COUNTDOWN_SEC = 3;
constexpr int RESULTS_SEC = 8;
constexpr std::size_t MAX_NAME = 16;

enum class PartyState : std::uint8_t { Lobby = 0, Countdown = 1, Playing = 2, Results = 3 };
enum class PRes { Ok, Same, BadState, NotLeader, NotReady, BadTarget };
enum class PEvent { None, Changed, Started, Ended, ToLobby };

struct Member
{
    std::uint8_t  num = 0;
    PlayerId      conn = 0;
    std::uint32_t joined_tick = 0;
    bool          ready = false;
    std::string   name;
    int           user_id = 0;
};

struct MatchResults
{
    std::uint16_t wave = 0;
    std::uint32_t score = 0;
    std::vector<std::pair<std::uint8_t, std::uint16_t>> points;   // номер, очки
};

class Party
{
public:
    explicit Party(std::string code) : code_(std::move(code)) {}

    const std::string& code() const { return code_; }
    PartyState state() const { return state_; }
    bool empty() const { return members_.empty(); }
    bool full() const { return members_.size() >= MAX_PARTY; }
    const std::vector<Member>& members() const { return members_; }
    const MatchResults& results() const { return results_; }
    std::uint8_t leader() const { return members_.empty() ? 0 : members_.front().num; }

    std::uint8_t countdown_sec() const
    {
        if (state_ != PartyState::Countdown) return 0;
        return static_cast<std::uint8_t>((timer_ + TICK_RATE - 1) / TICK_RATE);
    }

    // 0, если нельзя: нет места, идёт игра, пользователь забанен
    std::uint8_t add_member(PlayerId conn, std::uint32_t tick, int user_id, const std::string& name)
    {
        if (full() || state_ == PartyState::Playing || banned_.count(user_id)) return 0;
        const std::uint8_t num = free_num();
        members_.push_back({ num, conn, tick, false, sanitize(name), user_id });
        cancel_countdown();                       // новичок не готов: старт отменяется
        return num;
    }

    bool remove_member(PlayerId conn)
    {
        for (auto it = members_.begin(); it != members_.end(); ++it)
        {
            if (it->conn != conn) continue;
            if (world_) world_->remove_player(world_->get_player(it->num));
            members_.erase(it);
            cancel_countdown();                   // состав изменился (в том числе ушёл лидер)
            return true;
        }
        return false;
    }

    std::uint8_t num_of(PlayerId conn) const
    {
        for (const auto& m : members_) if (m.conn == conn) return m.num;
        return 0;
    }

    PRes set_ready(PlayerId conn, bool ready)
    {
        if (state_ != PartyState::Lobby && state_ != PartyState::Countdown) return PRes::BadState;
        for (auto& m : members_)
        {
            if (m.conn != conn) continue;
            if (m.ready == ready) return PRes::Same;
            m.ready = ready;
            if (!ready) cancel_countdown();
            return PRes::Ok;
        }
        return PRes::BadTarget;
    }

    PRes begin_countdown(PlayerId conn)
    {
        if (num_of(conn) != leader()) return PRes::NotLeader;
        if (state_ != PartyState::Lobby) return PRes::BadState;     // повторный Start игнорируется
        if (!everyone_ready()) return PRes::NotReady;
        state_ = PartyState::Countdown;
        timer_ = COUNTDOWN_SEC * TICK_RATE;
        return PRes::Ok;
    }

    // Кик только в лобби. Участника не удаляет: это делает менеджер через leave.
    PRes kick(PlayerId by, std::uint8_t num, PlayerId& target)
    {
        if (num_of(by) != leader()) return PRes::NotLeader;
        if (state_ != PartyState::Lobby) return PRes::BadState;
        if (num == leader()) return PRes::BadTarget;
        for (const auto& m : members_)
            if (m.num == num)
            {
                banned_.insert(m.user_id);
                target = m.conn;
                return PRes::Ok;
            }
        return PRes::BadTarget;
    }

    void set_input(std::uint8_t num, const InputState& in)
    {
        if (world_) world_->set_input(world_->get_player(num), in);
    }

    PEvent tick()
    {

        switch (state_)
        {
        case PartyState::Countdown:
            std::cout << "[party " << code_ << "] countdown timer=" << timer_ << std::endl;
            if (--timer_ > 0)
                return timer_ % TICK_RATE == 0 ? PEvent::Changed : PEvent::None;
            if (!everyone_ready()) { state_ = PartyState::Lobby; return PEvent::Changed; }  // за 3 с всё могло измениться
            start_world();
            return PEvent::Started;

        case PartyState::Playing:
            world_->tick();
            if (world_->game_over())
            {
                capture_results();
                world_.reset();                   // мир уничтожен
                state_ = PartyState::Results;
                timer_ = RESULTS_SEC * TICK_RATE;
                return PEvent::Ended;
            }
            return PEvent::None;

        case PartyState::Results:
            if (--timer_ > 0) return PEvent::None;
            for (auto& m : members_) m.ready = false;
            state_ = PartyState::Lobby;
            return PEvent::ToLobby;

        default:
            return PEvent::None;
        }
    }

    void write_world(proto::Writer& w) const { if (world_) world_->write_world(w); }

private:
    bool everyone_ready() const        // лидер готов всегда, минимум один игрок
    {
        if (members_.empty()) return false;
        for (std::size_t i = 1; i < members_.size(); ++i)
            if (!members_[i].ready) return false;
        return true;
    }

    void cancel_countdown() { if (state_ == PartyState::Countdown) { std::cout << "[party " << code_ << "] countdown CANCELLED" << std::endl; state_ = PartyState::Lobby; }; }

    void start_world()
    {
        world_ = std::make_unique<Instance>();
        for (const auto& m : members_) world_->add_player(m.num);
        state_ = PartyState::Playing;
    }

    void capture_results()
    {
        results_ = {};
        results_.wave = static_cast<std::uint16_t>(std::clamp(world_->wave(), 0, 65535));
        results_.score = static_cast<std::uint32_t>(std::max(world_->score(), 0));
        for (const auto& m : members_)
            results_.points.push_back({ m.num,
                static_cast<std::uint16_t>(std::clamp(world_->points_of(m.num), 0, 65535)) });
    }

    static std::string sanitize(const std::string& s)
    {
        std::string out;
        for (char c : s)
        {
            if (out.size() >= MAX_NAME) break;
            const unsigned char u = static_cast<unsigned char>(c);
            out.push_back((u >= 32 && u < 127) ? c : '?');
        }
        return out;
    }

    std::uint8_t free_num() const
    {
        for (std::uint8_t n = 1; n <= MAX_PARTY; ++n)
        {
            bool used = false;
            for (const auto& m : members_) if (m.num == n) { used = true; break; }
            if (!used) return n;
        }
        return 0;
    }

    std::string code_;
    std::vector<Member> members_;
    std::set<int> banned_;
    std::unique_ptr<Instance> world_;
    PartyState state_ = PartyState::Lobby;
    int timer_ = 0;
    MatchResults results_;
};