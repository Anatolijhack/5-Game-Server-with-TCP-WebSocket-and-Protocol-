
#pragma once

struct Position
{
    float x = 0.f;
    float y = 0.f;
};

struct Velocity
{
    float x = 0.f;
    float y = 0.f;
};

// —корость движени€ сущности
struct MoveStats
{
    float speed = 100.f;
};

struct Player {};

enum class EnemyKind
{
    Grunt,
    Fast,
    Tank,
    Rocketeer,
    Boss,
    Flamer
};

struct Enemy
{
    EnemyKind kind = EnemyKind::Grunt;
};

enum class WeaponType
{
    Pistol,
    Shotgun,
    Laser,
    Bazooka,
    Flamethrower
};
inline float baseCooldown(WeaponType t)
{
    switch (t)
    {
    case WeaponType::Pistol:       return 0.35f;
    case WeaponType::Shotgun:      return 0.55f;
    case WeaponType::Laser:        return 0.45f;
    case WeaponType::Bazooka:      return 1.6f;
    case WeaponType::Flamethrower: return 0.18f; // быстрые тики урона
    }
    return 0.35f;
}

struct Weapon
{
    WeaponType type = WeaponType::Pistol;
    float rateMul = 1.0f; // множитель перезар€дки: меньше 1 значит стрел€ет чаще. ћен€ют только баффы
    float timer = 0.0f;

    float cooldown() const { return baseCooldown(type) * rateMul; }
};
struct Flame { float life = 0.33f, maxLife = 0.33f; };
struct Bullet
{
    bool fromPlayer = true;
};

struct Beam
{
    float x1 = 0.f;
    float y1 = 0.f;
    float x2 = 0.f;
    float y2 = 0.f;
    float life = 0.f;
};

struct Health
{
    int hp = 1;
    int max = 1;
};

struct Shooter
{
    float cooldown = 1.5f;
    float timer = 0.0f;
};

struct InputState
{
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    bool shoot = false;
    bool restart = false;

    float aimX = 0.f;
    float aimY = 0.f;

    bool num1 = false;
    bool num2 = false;
    bool num3 = false;
    bool num4 = false;
    bool num5 = false;
};

struct Wall
{
    float w = 40.f;
    float h = 40.f;
};

struct Collider
{
    float w = 20.f;
    float h = 20.f;
};

struct GameData
{
    int score = 0;
    int wave = 0;
};

struct Explosive
{
    float radius = 60.f;
    int damage = 3;
};


struct Explosion
{
    float x = 0.f, y = 0.f, radius = 0.f;
    float life = 0.25f, maxLife = 0.25f;
};
struct BossAI
{
    float fireTimer = 2.0f;
    float rocketTimer = 4.0f;
};