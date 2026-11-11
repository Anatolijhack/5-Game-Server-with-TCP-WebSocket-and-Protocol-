#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <vector>
#include "Registry.h"
#include "Components.h"
#include "System.h"
#include "GameSystems.h"
#include "Game.h"          // ваш: UpgradeKind, applyUpgrade. Без SFML!
#include "PartyCore.h"
#include "Protocol.h"      // версия из папки net8a

// Общий мир для нескольких игроков (8a). Серверная копия логики GameScene + WaveClearScene.
// Нужны шаги 2-6 (ввод и таймеры в компонентах, смерть как состояние, цели врагов, пули, волны),
// в том числе перегрузка generateWalls со списком точек из шага 6.
//
// Имена методов совпадают с вашей версией: get_player, add_player, remove_player(Entity), set_input(Entity, ...).

constexpr int   TICK_RATE = 30;
constexpr float DT = 1.0f / TICK_RATE;
constexpr float ARENA_W = 800.f;      // как в SceneManager и BoundsSystem
constexpr float ARENA_H = 600.f;

constexpr std::size_t MAX_PARTY = 4;          // игроков в одном мире
constexpr float GAME_OVER_DELAY = 5.f;        // секунд до перезапуска, когда погибли все
constexpr float PICK_TIMEOUT = 10.f;          // секунд на выбор бонуса, потом выбирается случайный
constexpr float PICK_INPUT_DELAY = 0.2f;      // первые 0,2 с выбор не принимается ("долетевшее" нажатие)
constexpr float JOIN_PROTECTION = 2.f;        // секунд без урона от касаний после входа

inline std::int16_t q16(float v)
{
    return static_cast<std::int16_t>(std::lround(std::clamp(v, -32768.f, 32767.f)));
}

inline std::uint8_t t255(float life, float maxLife)
{
    if (maxLife <= 0.f) return 0;
    return static_cast<std::uint8_t>(std::clamp(life / maxLife, 0.f, 1.f) * 255.f);
}

// Фазы сцены на сервере (в снимок уходят номерами 0-3)
enum class Phase : std::uint8_t { Playing = 0, UpgradePick = 1, Victory = 2, GameOver = 3 };

class Instance
{
public:
    Instance() : systems_(reg_)
    {
        // Порядок систем ровно как в конструкторе GameScene. RenderSystem на сервере не нужен.
        systems_.push_module(std::make_shared<InputSystem>(reg_, data_));
        systems_.push_module(std::make_shared<EnemySystem>(reg_));
        systems_.push_module(std::make_shared<BossSystem>(reg_));
        systems_.push_module(std::make_shared<MoveSystem>(reg_));
        systems_.push_module(std::make_shared<CollisionSystem>(reg_, data_));
        systems_.push_module(std::make_shared<ContactDamageSystem>(reg_));
        systems_.push_module(std::make_shared<BoundsSystem>(reg_, ARENA_W, ARENA_H));
        systems_.push_module(std::make_shared<WallCollisionSystem>(reg_));
        systems_.push_module(std::make_shared<BeamSystem>(reg_));
        systems_.push_module(std::make_shared<WaveSystem>(reg_, data_, ARENA_W, ARENA_H));
        systems_.push_module(std::make_shared<BulletWallSystem>(reg_, data_));
        systems_.push_module(std::make_shared<ExplosionSystem>(reg_));
        systems_.push_module(std::make_shared<FlameSystem>(reg_));

        reset_world();           // стены БЕЗ игроков: игроков создаёт add_player
        systems_.Init();
        systems_.Start();
    }

    ~Instance() { systems_.Stop(); }

    // ---------------------------------------------------------------- игроки
    bool game_over() const { return phase_ == Phase::GameOver; }
    int  wave()  const { return data_.wave; }
    int  score() const { return data_.score; }
    int  points_of(std::uint32_t net_id) const
    {
        auto it = players_.find(net_id);
        if (it == players_.end()) return 0;
        const PlayerScore* ps = reg_.tryGet<PlayerScore>(it->second.e);
        return ps ? ps->points : 0;
    }

    Entity get_player(std::uint32_t net_id) const
    {
        auto it = players_.find(net_id);
        return it == players_.end() ? Entity{} : it->second.e;
    }

    // Пустая сущность, если мест нет
    Entity add_player(std::uint32_t net_id)
    {
        auto it = players_.find(net_id);
        if (it != players_.end()) return it->second.e;
        if (players_.size() >= MAX_PARTY) return Entity{};

        PlayerRec rec;
        rec.slot = free_slot();
        rec.e = make_player(spawn_point(rec.slot));
        reg_.get<ContactCooldown>(rec.e).timer = JOIN_PROTECTION;
        rec.picked = (phase_ == Phase::UpgradePick);   // пришедший во время выбора не задерживает остальных
        players_[net_id] = rec;
        return rec.e;
    }

    // Вызывать между тиками (не внутри systems_.Update)
    void remove_player(Entity e)
    {
        for (auto it = players_.begin(); it != players_.end(); ++it)
        {
            if (it->second.e == e)
            {
                if (reg_.alive(e)) reg_.destroy(e);
                players_.erase(it);
                return;
            }
        }
    }

    void set_input(Entity e, const InputState& input)
    {
        if (reg_.has<InputState>(e))
            reg_.get<InputState>(e) = input;
    }

    std::size_t player_count() const { return players_.size(); }

    // ---------------------------------------------------------------- тик

    void tick()
    {
        switch (phase_)
        {
        case Phase::Playing:     tick_playing();  break;
        case Phase::UpgradePick: tick_pick();     break;
        default:                 tick_gameover(); break;     // победа или поражение: ждём и начинаем заново
        }
    }

    // ---------------------------------------------------------------- снимок
    // Общий блок "мир" (одинаков для всех игроков). Формат описан в Protocol.h (папка net8a).

    void write_world(proto::Writer& w) const
    {
        w.u8(static_cast<std::uint8_t>(phase_));
        w.u8(static_cast<std::uint8_t>(optA_));
        w.u8(static_cast<std::uint8_t>(optB_));
        w.u8(phase_ == Phase::UpgradePick
            ? static_cast<std::uint8_t>(std::clamp(std::ceil(pick_timer_), 0.f, 255.f))
            : static_cast<std::uint8_t>(0));
        w.u16(static_cast<std::uint16_t>(std::clamp(data_.wave, 0, 65535)));
        w.u32(static_cast<std::uint32_t>(std::max(data_.score, 0)));

        // Игроки. Количество считаем заранее, чтобы оно совпало с числом записей.
        std::uint8_t n = 0;
        for (const auto& kv : players_)
        {
            const Entity e = kv.second.e;
            if (reg_.has<Position>(e) && reg_.has<Health>(e) && reg_.has<Weapon>(e)) ++n;
        }
        w.u8(n);

        for (const auto& kv : players_)
        {
            const Entity e = kv.second.e;
            const Position* pp = reg_.tryGet<Position>(e);
            const Health* hp = reg_.tryGet<Health>(e);
            const Weapon* wp = reg_.tryGet<Weapon>(e);
            if (!pp || !hp || !wp) continue;

            const PlayerScore* ps = reg_.tryGet<PlayerScore>(e);

            std::uint8_t flags = 0;
            if (hp->hp > 0) flags |= 1;
            if (kv.second.picked) flags |= 2;

            w.u32(kv.first);
            w.i16(q16(pp->x));
            w.i16(q16(pp->y));
            w.u8(static_cast<std::uint8_t>(std::clamp(hp->hp, 0, 255)));
            w.u8(static_cast<std::uint8_t>(std::clamp(hp->max, 0, 255)));
            w.u8(static_cast<std::uint8_t>(wp->type));
            w.u8(flags);
            w.u16(static_cast<std::uint16_t>(ps ? std::clamp(ps->points, 0, 65535) : 0));
        }

        // Враги
        const auto enemies = reg_.entitiesWith<Enemy, Position, Health>();
        w.u16(static_cast<std::uint16_t>(enemies.size()));
        for (Entity e : enemies)
        {
            const Position& p = reg_.get<Position>(e);
            const Health& h = reg_.get<Health>(e);
            w.u32((e.index & 0x00FFFFFFu) | ((e.generation & 0xFFu) << 24));
            w.u8(static_cast<std::uint8_t>(reg_.get<Enemy>(e).kind));
            w.i16(q16(p.x));
            w.i16(q16(p.y));
            w.u16(static_cast<std::uint16_t>(std::clamp(h.hp, 0, 65535)));
            w.u16(static_cast<std::uint16_t>(std::clamp(h.max, 0, 65535)));
        }

        // Пули (не больше 300)
        const auto bullets = reg_.entitiesWith<Bullet, Position>();
        const std::size_t nb = std::min<std::size_t>(bullets.size(), 300);
        w.u16(static_cast<std::uint16_t>(nb));
        for (std::size_t i = 0; i < nb; ++i)
        {
            const Position& p = reg_.get<Position>(bullets[i]);
            std::uint8_t flags = 0;
            if (reg_.get<Bullet>(bullets[i]).fromPlayer) flags |= 1;
            if (reg_.has<Explosive>(bullets[i]))         flags |= 2;
            w.u8(flags);
            w.i16(q16(p.x));
            w.i16(q16(p.y));
        }

        // Лучи (не больше 16)
        const auto beams = reg_.entitiesWith<Beam>();
        const std::size_t nbeams = std::min<std::size_t>(beams.size(), 16);
        w.u8(static_cast<std::uint8_t>(nbeams));
        for (std::size_t i = 0; i < nbeams; ++i)
        {
            const Beam& b = reg_.get<Beam>(beams[i]);
            w.i16(q16(b.x1)); w.i16(q16(b.y1));
            w.i16(q16(b.x2)); w.i16(q16(b.y2));
        }

        // Взрывы (не больше 32)
        const auto explosions = reg_.entitiesWith<Explosion>();
        const std::size_t nex = std::min<std::size_t>(explosions.size(), 32);
        w.u8(static_cast<std::uint8_t>(nex));
        for (std::size_t i = 0; i < nex; ++i)
        {
            const Explosion& x = reg_.get<Explosion>(explosions[i]);
            w.i16(q16(x.x));
            w.i16(q16(x.y));
            w.u16(static_cast<std::uint16_t>(std::clamp(x.radius, 0.f, 65535.f)));
            w.u8(t255(x.life, x.maxLife));
        }

        // Огонь (не больше 200 частиц)
        const auto flames = reg_.entitiesWith<Flame, Position>();
        const std::size_t nf = std::min<std::size_t>(flames.size(), 200);
        w.u16(static_cast<std::uint16_t>(nf));
        for (std::size_t i = 0; i < nf; ++i)
        {
            const Position& p = reg_.get<Position>(flames[i]);
            const Flame& f = reg_.get<Flame>(flames[i]);
            w.i16(q16(p.x));
            w.i16(q16(p.y));
            w.u8(t255(f.life, f.maxLife));
        }

        // Стены (меняются каждую волну)
        const auto walls = reg_.entitiesWith<Wall, Position>();
        const std::size_t nw = std::min<std::size_t>(walls.size(), 32);
        w.u8(static_cast<std::uint8_t>(nw));
        for (std::size_t i = 0; i < nw; ++i)
        {
            const Position& p = reg_.get<Position>(walls[i]);
            const Wall& wl = reg_.get<Wall>(walls[i]);
            w.i16(q16(p.x)); w.i16(q16(p.y));
            w.i16(q16(wl.w)); w.i16(q16(wl.h));
        }
    }

private:
    struct PlayerRec
    {
        Entity e;
        int slot = 0;          // номер места: от него зависит точка появления
        bool picked = false;   // сделал ли выбор бонуса в текущей паузе
    };

    // ---------------------------------------------------------------- Playing

    void tick_playing()
    {
        systems_.Update(DT);

        if (players_.empty()) return;

        // Все игроки погибли: ждём GAME_OVER_DELAY и начинаем заново
        if (!anyPlayerAlive(reg_))
        {
            phase_ = Phase::GameOver;
            over_timer_ = 0.f;
            return;
        }

        bool anyEnemy = false;
        reg_.each<Enemy>([&](Entity, Enemy&) { anyEnemy = true; });

        // Волна только что зачищена (враги были, и вот их не стало) -> выбор бонуса
        if (hadEnemies_ && !anyEnemy && data_.wave > 0)
            enter_pick();
        hadEnemies_ = anyEnemy;
    }

    // ---------------------------------------------------------------- выбор бонуса

    void enter_pick()
    {
        optA_ = random_upgrade();
        do { optB_ = random_upgrade(); } while (optB_ == optA_);

        pick_delay_ = PICK_INPUT_DELAY;
        pick_timer_ = PICK_TIMEOUT;
        for (auto& kv : players_) kv.second.picked = false;     // ВАЖНО: сбросить выбор у всех

        phase_ = Phase::UpgradePick;
    }

    void tick_pick()
    {
        pick_delay_ -= DT;
        pick_timer_ -= DT;

        bool allDone = true;
        for (auto& kv : players_)
        {
            PlayerRec& rec = kv.second;
            if (rec.picked) continue;

            if (!isAlivePlayer(reg_, rec.e))     // погибший бонус не выбирает (вернётся в начале волны)
            {
                rec.picked = true;
                continue;
            }

            if (pick_delay_ <= 0.f && reg_.has<InputState>(rec.e))
            {
                const InputState& in = reg_.get<InputState>(rec.e);
                const int pick = in.num1 ? 1 : in.num2 ? 2 : 0;
                if (pick != 0)
                {
                    applyUpgrade(reg_, rec.e, pick == 1 ? optA_ : optB_);
                    rec.picked = true;
                    continue;
                }
            }
            allDone = false;
        }

        // Время вышло: молчавшим достаётся случайный из двух бонусов
        if (!allDone && pick_timer_ <= 0.f)
        {
            for (auto& kv : players_)
            {
                if (kv.second.picked) continue;
                applyUpgrade(reg_, kv.second.e, std::uniform_int_distribution<int>(0, 1)(rng_) ? optA_ : optB_);
                kv.second.picked = true;
            }
            allDone = true;
        }

        if (allDone) phase_ = Phase::Playing;     // мир не пересоздаётся: игра продолжается
    }

    UpgradeKind random_upgrade()
    {
        static const UpgradeKind pool[] = {
            UpgradeKind::MaxHp, UpgradeKind::Heal, UpgradeKind::FireRate,
            UpgradeKind::Speed, UpgradeKind::Shotgun, UpgradeKind::Laser,
            UpgradeKind::Bazooka
        };
        return pool[std::uniform_int_distribution<int>(0, (int)(sizeof(pool) / sizeof(pool[0])) - 1)(rng_)];
    }

    // ---------------------------------------------------------------- конец игры и перезапуск

    void tick_gameover()
    {
        over_timer_ += DT;
        if (over_timer_ >= GAME_OVER_DELAY) restart();
    }

    // Новый мир с теми же игроками. После reg.clear() все старые сущности недействительны,
    // поэтому игроков создаём заново и обновляем таблицу.
    void restart()
    {
        reset_world();
        for (auto& kv : players_)
        {
            kv.second.e = make_player(spawn_point(kv.second.slot));
            kv.second.picked = false;
        }
    }

    // Очистить мир и расставить стены (БЕЗ игроков)
    void reset_world()
    {
        reg_.clear();
        replaceWalls(reg_, generateWalls(ARENA_W, ARENA_H, all_spawn_points(), 5));

        data_.score = 0;
        data_.wave = 0;
        hadEnemies_ = false;
        phase_ = Phase::Playing;
        over_timer_ = 0.f;
    }

    // Набор компонентов как в resetWorld (Game.h). Если измените игрока там, измените и здесь.
    Entity make_player(const Position& at)
    {
        Entity p = reg_.create();
        reg_.add<Player>(p);
        reg_.add<Position>(p, at.x, at.y);
        reg_.add<Velocity>(p);
        reg_.add<Health>(p, 5, 5);
        reg_.add<Collider>(p);
        reg_.add<Weapon>(p);
        reg_.add<MoveStats>(p);
        reg_.add<InputState>(p);
        reg_.add<ContactCooldown>(p);
        return p;
    }

    // Места появления: в ряд около центра поля, чтобы игроки не стояли друг на друге
    static Position spawn_point(int slot)
    {
        return Position{ 390.f + (static_cast<float>(slot) - 1.5f) * 40.f, 290.f };
    }

    static std::vector<Position> all_spawn_points()
    {
        std::vector<Position> v;
        for (int i = 0; i < static_cast<int>(MAX_PARTY); ++i) v.push_back(spawn_point(i));
        return v;
    }

    int free_slot() const
    {
        for (int s = 0; s < static_cast<int>(MAX_PARTY); ++s)
        {
            bool used = false;
            for (const auto& kv : players_)
                if (kv.second.slot == s) { used = true; break; }
            if (!used) return s;
        }
        return 0;
    }

    Registry reg_;
    GameData data_;
    ModuleRegistry systems_;
    std::mt19937 rng_{ std::random_device{}() };

    std::map<std::uint32_t, PlayerRec> players_;      // сетевой id -> игрок в мире

    Phase phase_ = Phase::Playing;
    UpgradeKind optA_ = UpgradeKind::MaxHp, optB_ = UpgradeKind::Heal;
    float pick_delay_ = 0.f;
    float pick_timer_ = 0.f;
    float over_timer_ = 0.f;
    bool hadEnemies_ = false;
};