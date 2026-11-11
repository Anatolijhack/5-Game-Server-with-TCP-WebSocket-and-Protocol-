#pragma once
#include <vector>
#include "Registry.h"
#include "Components.h"

// Основа для игры нескольких игроков (пати). Ничего не меняет в одиночной игре:
// только добавляет компоненты и функции, которыми системы будут пользоваться по шагам.
// Положите рядом с Components.h и подключайте там, где системы раньше вызывали findPlayer().

// ---------------------------------------------------------------------------
// Компоненты

// Кто выпустил пулю, луч, взрыв или огонь. Для врагов пустая сущность.
// Нужен, чтобы начислять очки по игрокам и (при желании) запретить дружественный огонь.
struct Owner
{
    Entity who;
};

// Секунда неуязвимости к касаниям врагов. Раньше таймер лежал внутри
// ContactDamageSystem (один на всех), теперь у каждого игрока свой.
struct ContactCooldown
{
    float timer = 0.f;
};

// Личные очки игрока (общий счёт пати остаётся в GameData::score)
struct PlayerScore
{
    int kills = 0;
    int points = 0;
};

// ---------------------------------------------------------------------------
// Правило пати: игрок не удаляется при гибели. hp <= 0 значит "погиб до следующей волны".

inline bool isAlivePlayer(const Registry& reg, Entity e)
{
    if (!reg.has<Player>(e)) return false;
    const Health* h = reg.tryGet<Health>(e);
    return h && h->hp > 0;
}

// Все игроки (живые и погибшие)
inline std::vector<Entity> allPlayers(const Registry& reg)
{
    return reg.entitiesWith<Player>();
}

// Только живые игроки
inline std::vector<Entity> alivePlayers(const Registry& reg)
{
    std::vector<Entity> out;
    reg.each<Player, Health>([&](Entity e, Player&, Health& h)
        {
            if (h.hp > 0) out.push_back(e);
        });
    return out;
}

inline int playerCount(const Registry& reg)
{
    int n = 0;
    reg.each<Player>([&](Entity, Player&) { ++n; });
    return n;
}

inline bool anyPlayerAlive(const Registry& reg)
{
    bool alive = false;
    reg.each<Player, Health>([&](Entity, Player&, Health& h)
        {
            if (h.hp > 0) alive = true;
        });
    return alive;
}

// Первый живой игрок. Для мест, где система пока не переделана и ей достаточно "какого-нибудь" игрока.
inline Entity firstAlivePlayer(const Registry& reg)
{
    Entity result;
    reg.each<Player, Health>([&](Entity e, Player&, Health& h)
        {
            if (h.hp > 0 && result.isNull()) result = e;
        });
    return result;
}

// Ближайший живой игрок к точке (x, y). Если живых нет, возвращает пустую сущность.
// outDist2 (необязательно) получает квадрат расстояния.
inline Entity nearestAlivePlayer(const Registry& reg, float x, float y, float* outDist2 = nullptr)
{
    Entity best;
    float bestD2 = 1e30f;

    reg.each<Player, Position, Health>([&](Entity e, Player&, Position& p, Health& h)
        {
            if (h.hp <= 0) return;
            const float dx = p.x - x, dy = p.y - y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; best = e; }
        });

    if (outDist2) *outDist2 = bestD2;
    return best;
}

// Ближайший живой игрок с "липкостью": остаёмся на текущей цели, пока другой игрок
// не окажется заметно ближе (stickiness = 0.7 значит "другой должен быть ближе в 1/0.7 раза").
// Без этого враг дёргается между двумя почти равноудалёнными игроками.
inline Entity pickTarget(const Registry& reg, Entity current, float x, float y, float stickiness = 0.7f)
{
    float bestD2 = 0.f;
    const Entity best = nearestAlivePlayer(reg, x, y, &bestD2);

    if (!current.isNull() && isAlivePlayer(reg, current) && current != best)
    {
        if (const Position* cp = reg.tryGet<Position>(current))
        {
            const float dx = cp->x - x, dy = cp->y - y;
            const float curD2 = dx * dx + dy * dy;
            if (bestD2 > curD2 * stickiness * stickiness)
                return current;              // новый кандидат не настолько ближе, чтобы менять цель
        }
    }
    return best;
}