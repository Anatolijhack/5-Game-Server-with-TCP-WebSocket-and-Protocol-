#pragma once
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>
#include "System.h"
#include <random>
#include "PartyCore.h"

struct WallRect { float x, y, w, h; };

// Общий генератор случайных чисел для генерации карты
inline std::mt19937& gameRng()
{
	static std::mt19937 rng{ std::random_device{}() };
	return rng;
}

// Пересекаются ли прямоугольники (pad раздвигает первый, чтобы получить зазор)
inline bool rectsOverlap(float ax, float ay, float aw, float ah,
	float bx, float by, float bw, float bh, float pad = 0.f)
{
	return ax - pad < bx + bw && ax + aw + pad > bx &&
		ay - pad < by + bh && ay + ah + pad > by;
}

// Раскладывает до `count` стен: горизонтальные или вертикальные полосы.
// Не попадают в зону вокруг игрока и не лежат вплотную друг к другу.
inline std::vector<WallRect> generateWalls(float screenW, float screenH,
	float playerX, float playerY, int count)
{
	auto& rng = gameRng();
	auto rnd = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };

	const float edge = 60.f;   // отступ от краёв окна: враги приходят оттуда, вход должен быть свободен
	const float safeR = 70.f;  // свободная зона вокруг игрока
	const float gap = 40.f;    // минимальный проход между стенами

	std::vector<WallRect> out;
	for (int i = 0; i < count; ++i)
	{
		for (int attempt = 0; attempt < 30; ++attempt) // 30 попыток найти свободное место
		{
			bool horizontal = rnd(0.f, 1.f) < 0.5f;
			float w = horizontal ? rnd(90.f, 240.f) : 30.f;
			float h = horizontal ? 30.f : rnd(90.f, 200.f);
			float x = rnd(edge, screenW - edge - w);
			float y = rnd(edge, screenH - edge - h);

			if (rectsOverlap(x, y, w, h, playerX, playerY, 20.f, 20.f, safeR)) continue;

			bool clash = false;
			for (auto& o : out)
				if (rectsOverlap(x, y, w, h, o.x, o.y, o.w, o.h, gap)) { clash = true; break; }
			if (clash) continue;

			out.push_back({ x, y, w, h });
			break;
		}
	}
	return out;
}
inline std::vector<WallRect> generateWalls(
	float screenW,
	float screenH,
	const std::vector<Position>& spawnPoints,
	int count)
{
	auto& rng = gameRng();
	auto rnd = [&](float a, float b) {
		return std::uniform_real_distribution<float>(a, b)(rng);
		};

	const float edge = 60.f;
	const float safeR = 70.f;
	const float gap = 40.f;

	std::vector<WallRect> out;

	for (int i = 0; i < count; ++i)
	{
		for (int attempt = 0; attempt < 30; ++attempt)
		{
			bool horizontal = rnd(0.f, 1.f) < 0.5f;

			float w = horizontal ? rnd(90.f, 240.f) : 30.f;
			float h = horizontal ? 30.f : rnd(90.f, 200.f);

			float x = rnd(edge, screenW - edge - w);
			float y = rnd(edge, screenH - edge - h);

			// Проверяем безопасную зону вокруг ВСЕХ игроков
			bool nearPlayer = false;

			for (const Position& p : spawnPoints)
			{
				if (rectsOverlap(
					x, y, w, h,
					p.x, p.y, 20.f, 20.f,
					safeR))
				{
					nearPlayer = true;
					break;
				}
			}

			if (nearPlayer)
				continue;

			// Проверяем пересечение с другими стенами
			bool clash = false;

			for (const auto& o : out)
			{
				if (rectsOverlap(
					x, y, w, h,
					o.x, o.y, o.w, o.h,
					gap))
				{
					clash = true;
					break;
				}
			}

			if (clash)
				continue;

			out.push_back({ x, y, w, h });
			break;
		}
	}

	return out;
}

// Удаляет все старые стены и создаёт новые. Вызывать из главного потока.
inline void replaceWalls(Registry& reg, const std::vector<WallRect>& rects)
{
	for (Entity w : reg.entitiesWith<Wall>()) reg.destroy(w);
	for (auto& r : rects)
	{
		Entity e = reg.create();
		reg.add<Position>(e, r.x, r.y);
		reg.add<Wall>(e, r.w, r.h);
	}
}

inline Entity findPlayer(const Registry& reg)
{
	Entity result;
	reg.each<Player>([&](Entity e, Player&) { result = e; });
	return result;
}

struct Box
{
	float x, y, w, h;
	float cx() const { return x + w / 2.f; }
	float cy() const { return y + h / 2.f; }
};

// Прямоугольник сущности: позиция + Collider (если нет, считаем 20x20)
inline Box boxOf(const Registry& reg, Entity e)
{
	const Position& p = reg.get<Position>(e);
	float w = 20.f, h = 20.f;
	if (auto* c = reg.tryGet<Collider>(e)) { w = c->w; h = c->h; }
	return { p.x, p.y, w, h };
}
// Общий урон по врагу: и обычные пули, и лазер идут через эту функцию,
// чтобы очки начислялись один раз и не дублировалась логика смерти.
inline void damageEnemy(Registry& reg, GameData& data, Entity target, int dmg)
{
	if (!reg.alive(target)) return;
	auto* hp = reg.tryGet<Health>(target);
	if (!hp || hp->hp <= 0) return; // уже помечен на удаление в этом кадре

	hp->hp -= dmg;
	if (hp->hp <= 0)
	{
		if (auto* en = reg.tryGet<Enemy>(target))
			data.score += (en->kind == EnemyKind::Boss) ? 100 : 10;
		reg.destroyLater(target);
	}
}

inline void explode(Registry& reg, GameData& data, float cx, float cy,
	float radius, int dmg, bool fromPlayer)
{
	const float r2 = radius * radius;

	// Свой взрыв бьёт врагов. Взрыв вражеской ракеты врагов не трогает.
	if (fromPlayer)
	{
		for (Entity e : reg.entitiesWith<Enemy, Position>())
		{
			Box b = boxOf(reg, e);
			float dx = b.cx() - cx, dy = b.cy() - cy;
			float reach = radius + b.w / 2.f; // у большого врага больше площадь попадания
			if (dx * dx + dy * dy <= reach * reach)
				damageEnemy(reg, data, e, dmg);
		}
	}

	// Игрока вражеский взрыв ранит всегда, свой - если включён friendly fire
	const bool friendlyFire = true;
	if (!fromPlayer || friendlyFire)
	{
		Entity pl = findPlayer(reg);
		auto* pp = pl.isNull() ? nullptr : reg.tryGet<Position>(pl);
		auto* hp = pl.isNull() ? nullptr : reg.tryGet<Health>(pl);
		if (pp && hp && hp->hp > 0)              // <-- добавлено: hp > 0
		{
			float dx = pp->x + 10.f - cx, dy = pp->y + 10.f - cy;
			if (dx * dx + dy * dy <= r2)
				hp->hp--;                 // <-- строку "if (hp->hp <= 0) reg.destroyLater(pl);" удалить
		}
	}

	Entity fx = reg.create();
	reg.add<Explosion>(fx, cx, cy, radius);
}

// --------------------------------------------------------------------------
// ШАГ 2. Вставьте этот класс в GameSystems.h ВМЕСТО старого class InputSystem
// (весь класс, от "class InputSystem" до закрывающей "};"). Это не отдельный заголовок.
// Над ним в GameSystems.h должна быть строка:  #include "PartyCore.h"
//
// Что изменилось:
//   * ввод больше не поле системы, а компонент InputState на каждом игроке;
//   * Update обходит всех игроков, а не одного (findPlayer здесь больше не нужен);
//   * погибший игрок (hp <= 0) стоит и не стреляет.
// Поведение игры с ОДНИМ игроком не меняется: SetInput() записывает ввод в компонент игрока.
// Сервер для пати будет записывать InputState каждого игрока напрямую, минуя SetInput().

class InputSystem : public System
{
	Registry& reg;
	GameData& data; // нужна для мгновенного урона лазера и огня

public:
	InputSystem(Registry& r, GameData& d) : reg(r), data(d) {}

	// Одиночная игра и клиент: ввод получает игрок (на практике он один).
	// Компонент добавляется при первом вызове, поэтому resetWorld менять не нужно.
	void SetInput(const InputState& in) override
	{
		for (Entity p : reg.entitiesWith<Player>())
		{
			if (!reg.has<InputState>(p)) reg.add<InputState>(p);
			reg.get<InputState>(p) = in;
		}
	}

	void Update(float dt, ThreadPool&, CommandBuffer& cmd) override
	{
		for (Entity player : reg.entitiesWith<Player, InputState>())
			updatePlayer(player, reg.get<InputState>(player), dt, cmd);   // ввод копируется
	}

private:
	std::mt19937 rng{ std::random_device{}() };
	float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }

	// Тело бывшего Update: то же самое, но для одного заданного игрока.
	void updatePlayer(Entity player, InputState input, float dt, CommandBuffer& cmd)
	{
		float speed = 100.f;
		if (auto* ms = reg.tryGet<MoveStats>(player)) speed = ms->speed;

		auto* vel = reg.tryGet<Velocity>(player);
		if (vel) vel->x = vel->y = 0.f;

		if (!isAlivePlayer(reg, player)) return;   // погибший стоит и не стреляет

		if (vel)
		{
			if (input.left)  vel->x = -speed;
			if (input.right) vel->x = speed;
			if (input.up)    vel->y = -speed;
			if (input.down)  vel->y = speed;
		}

		Weapon* weapon = reg.tryGet<Weapon>(player);
		if (!weapon) return;

		// Клавиши 1-5 во время боя меняют оружие (между волнами те же клавиши выбирают бонус)
		if (input.num1) weapon->type = WeaponType::Pistol;
		if (input.num2) weapon->type = WeaponType::Shotgun;
		if (input.num3) weapon->type = WeaponType::Laser;
		if (input.num4) weapon->type = WeaponType::Bazooka;
		if (input.num5) weapon->type = WeaponType::Flamethrower;

		weapon->timer -= dt;

		auto* pos = reg.tryGet<Position>(player);
		if (!input.shoot || weapon->timer > 0.f || !pos) return;

		// Центр игрока (квадрат 20x20) -> курсор
		float cx = pos->x + 10.f, cy = pos->y + 10.f;
		float dx = input.aimX - cx, dy = input.aimY - cy;
		float len = std::sqrt(dx * dx + dy * dy);
		if (len < 1.f) return; // курсор прямо на игроке: направления нет
		dx /= len;
		dy /= len;

		switch (weapon->type)
		{
		case WeaponType::Pistol:
			fireBullet(cmd, cx, cy, dx, dy, 200.f);
			break;

		case WeaponType::Shotgun:
		{
			const int pellets = 5;
			const float spread = 0.5f; // радианы, суммарный веер
			float baseAngle = std::atan2(dy, dx);
			for (int i = 0; i < pellets; ++i)
			{
				float t = (float)i / (pellets - 1) - 0.5f; // от -0.5 до 0.5
				float angle = baseAngle + t * spread;
				fireBullet(cmd, cx, cy, std::cos(angle), std::sin(angle), 220.f);
			}
			break;
		}

		case WeaponType::Laser:
			fireLaser(cx, cy, dx, dy);
			break;
		case WeaponType::Bazooka:
			fireRocket(cmd, cx, cy, dx, dy, 120.f);
			break;
		case WeaponType::Flamethrower:
			fireFlame(cmd, cx, cy, dx, dy);
			break;
		}

		weapon->timer = weapon->cooldown();
	}

	void fireFlame(CommandBuffer& cmd, float cx, float cy, float dx, float dy)
	{
		const float range = 110.f;
		const float cosHalf = std::cos(0.45f); // полуширина конуса около 26 градусов

		// 1. Урон: все враги внутри конуса получают 1 урон за тик
		for (Entity e : reg.entitiesWith<Enemy, Position>())
		{
			Box b = boxOf(reg, e);
			float ex = b.cx() - cx, ey = b.cy() - cy;
			float dist = std::sqrt(ex * ex + ey * ey);
			if (dist > range + b.w / 2.f) continue;           // слишком далеко
			bool inCone = dist < 1.f || (ex * dx + ey * dy) / dist >= cosHalf; // косинус угла
			if (inCone) damageEnemy(reg, data, e, 1);
		}

		// 2. Визуал: несколько частиц с разбросом
		struct P { float x, y, vx, vy; };
		std::vector<P> ps;
		const float baseAngle = std::atan2(dy, dx);
		for (int i = 0; i < 5; ++i)
		{
			float a = baseAngle + rnd(-0.4f, 0.4f);
			float s = 330.f * rnd(0.7f, 1.0f);
			ps.push_back({ cx + dx * 15.f, cy + dy * 15.f, std::cos(a) * s, std::sin(a) * s });
		}

		cmd.add([ps](Registry& r)
			{
				for (const P& p : ps)
				{
					Entity f = r.create();
					r.add<Position>(f, p.x, p.y);
					r.add<Velocity>(f, p.vx, p.vy);
					r.add<Flame>(f);
				}
			});
	}

	void fireBullet(CommandBuffer& cmd, float cx, float cy, float dx, float dy, float bulletSpeed)
	{
		const float spawnDist = 20.f, half = 3.f;
		float bx = cx + dx * spawnDist - half, by = cy + dy * spawnDist - half;
		float vx = dx * bulletSpeed, vy = dy * bulletSpeed;

		cmd.add([=](Registry& r)
			{
				Entity b = r.create();
				r.add<Position>(b, bx, by);
				r.add<Velocity>(b, vx, vy);
				r.add<Bullet>(b, true);
			});
	}

	// Мгновенный луч: находит ближайшего врага на пути и сразу бьёт по нему.
	// Стены луч пока не перекрывают - это упрощение, укрытие от лазера не защищает.
	void fireLaser(float cx, float cy, float dx, float dy)
	{
		const float range = 700.f;
		float bestT = range;
		Entity best;

		reg.each<Enemy, Position>([&](Entity e, Enemy&, Position&)
			{
				Box eb = boxOf(reg, e);
				float ecx = eb.cx(), ecy = eb.cy();
				float t = (ecx - cx) * dx + (ecy - cy) * dy; // проекция центра врага на луч
				if (t < 0.f || t > bestT) return;

				float px = cx + dx * t, py = cy + dy * t;
				float dist2 = (px - ecx) * (px - ecx) + (py - ecy) * (py - ecy);
				float hitR = std::max(14.f, eb.w / 2.f);
				if (dist2 < hitR * hitR)
				{
					bestT = t;
					best = e;
				}
			});

		float endX = cx + dx * bestT, endY = cy + dy * bestT;
		Entity beam = reg.create();
		reg.add<Beam>(beam, cx, cy, endX, endY, 0.08f);

		if (!best.isNull())
			damageEnemy(reg, data, best, 2);
	}

	void fireRocket(CommandBuffer& cmd, float cx, float cy, float dx, float dy, float rocketSpeed)
	{
		const float spawnDist = 20.f, half = 3.f;
		float bx = cx + dx * spawnDist - half, by = cy + dy * spawnDist - half;
		float vx = dx * rocketSpeed, vy = dy * rocketSpeed;

		cmd.add([=](Registry& r)
			{
				Entity b = r.create();
				r.add<Position>(b, bx, by);
				r.add<Velocity>(b, vx, vy);
				r.add<Bullet>(b, true);
				r.add<Explosive>(b); // это и делает пулю ракетой
			});
	}
};

// --------------------------------------------------------------------------
// Просто гасит луч через пару кадров - самой отрисовкой занимается RenderSystem.
class BeamSystem : public System
{
	Registry& reg;

public:
	explicit BeamSystem(Registry& r) : reg(r) {}

	void Update(float dt, ThreadPool&, CommandBuffer&) override
	{
		reg.each<Beam>([&](Entity e, Beam& b)
			{
				b.life -= dt;
				if (b.life <= 0.f) reg.destroyLater(e);
			});
	}
};

// --------------------------------------------------------------------------
class EnemySystem : public System
{
	Registry& reg;
	const float baseSpeed = 50.f;
	const float avoidStrength = 1.5f;
	const float avoidRadius2 = 1600.f;
	const float flameRange = 90.f; // дальность огня врага

	static float speedMul(EnemyKind k)
	{
		switch (k)
		{
		case EnemyKind::Fast:      return 1.8f;
		case EnemyKind::Tank:      return 0.6f;
		case EnemyKind::Rocketeer: return 0.8f;
		case EnemyKind::Flamer:    return 1.1f;
		default:                   return 1.0f;
		}
	}

	// Частицы пламени (только картинка). Скорость 270 x жизнь 0,33 с = около 90 px, как flameRange
	void spawnFlames(CommandBuffer& cmd, float cx, float cy, float dx, float dy)
	{
		struct P { float x, y, vx, vy; };
		std::vector<P> ps;
		const float base = std::atan2(dy, dx);
		const float offsets[4] = { -0.3f, -0.1f, 0.1f, 0.3f };
		for (float o : offsets)
		{
			float a = base + o;
			ps.push_back({ cx + dx * 15.f, cy + dy * 15.f, std::cos(a) * 270.f, std::sin(a) * 270.f });
		}

		cmd.add([ps](Registry& r)
			{
				for (const P& p : ps)
				{
					Entity f = r.create();
					r.add<Position>(f, p.x, p.y);
					r.add<Velocity>(f, p.vx, p.vy);
					r.add<Flame>(f);
				}
			});
	}

public:
	explicit EnemySystem(Registry& r) : reg(r) {}

	void Update(float dt, ThreadPool&, CommandBuffer& cmd) override
	{
		Entity player = findPlayer(reg);
		auto* pp = player.isNull() ? nullptr : reg.tryGet<Position>(player);
		if (!pp) return;
		const Position target = *pp;

		auto enemies = reg.entitiesWith<Enemy, Position, Velocity>();

		for (Entity e : enemies)
		{
			const Position pos = reg.get<Position>(e);
			const EnemyKind kind = reg.get<Enemy>(e).kind;
			if (kind == EnemyKind::Boss) continue; // боссом управляет BossSystem

			const float speed = baseSpeed * speedMul(kind);

			float dx = target.x - pos.x;
			float dy = target.y - pos.y;
			float len = std::sqrt(dx * dx + dy * dy);
			bool hasDir = len > 0.f;
			if (hasDir) { dx /= len; dy /= len; }

			// Ракетчик останавливается в 250 px, огнемётчик в 70 px
			float chase = 1.f;
			if (kind == EnemyKind::Rocketeer && len < 250.f) chase = 0.f;
			if (kind == EnemyKind::Flamer && len < 70.f)  chase = 0.f;

			float avoidX = 0.f, avoidY = 0.f;
			for (Entity other : enemies)
			{
				if (other == e) continue;
				const Position& op = reg.get<Position>(other);
				float ddx = pos.x - op.x, ddy = pos.y - op.y;
				float d2 = ddx * ddx + ddy * ddy;
				if (d2 < avoidRadius2 && d2 > 0.f)
				{
					float d = std::sqrt(d2);
					avoidX += ddx / d;
					avoidY += ddy / d;
				}
			}

			Velocity& vel = reg.get<Velocity>(e);
			vel.x = dx * speed * chase + avoidX * speed * avoidStrength;
			vel.y = dy * speed * chase + avoidY * speed * avoidStrength;

			if (auto* sh = reg.tryGet<Shooter>(e))
			{
				sh->timer -= dt;
				if (sh->timer <= 0.f && hasDir)
				{
					if (kind == EnemyKind::Flamer)
					{
						// Огонь, а не пули: бьёт, только если игрок в радиусе
						if (len <= flameRange)
						{
							sh->timer = sh->cooldown;
							if (auto* php = reg.tryGet<Health>(player))
							{
								if (php->hp > 0) php->hp--;
							}
							spawnFlames(cmd, pos.x + 10.f, pos.y + 10.f, dx, dy);
						}
						// вне радиуса таймер остаётся готовым: выстрелит сразу, как подойдёт
					}
					else
					{
						sh->timer = sh->cooldown;
						const float offset = 40.f;
						float bx = pos.x + dx * offset, by = pos.y + dy * offset;

						const bool rocket = kind == EnemyKind::Rocketeer;
						const float bulletSpeed = rocket ? 120.f : 200.f;
						float vx = dx * bulletSpeed, vy = dy * bulletSpeed;

						cmd.add([=](Registry& r)
							{
								Entity b = r.create();
								r.add<Position>(b, bx, by);
								r.add<Velocity>(b, vx, vy);
								r.add<Bullet>(b, false);
								if (rocket) r.add<Explosive>(b, 50.f, 1);
							});
					}
				}
			}
		}
	}
};
// --------------------------------------------------------------------------
// Каждая сущность обрабатывается ровно одной задачей и пишется напрямую:
// ни командного буфера, ни гонок.
class MoveSystem : public System
{
	Registry& reg;

public:
	explicit MoveSystem(Registry& r) : reg(r) {}

	void Update(float dt, ThreadPool& pool, CommandBuffer&) override
	{
		auto entities = reg.entitiesWith<Position, Velocity>();

		parallel_for_chunks(pool, entities.size(), 256, [&](size_t, size_t begin, size_t end)
			{
				for (size_t j = begin; j < end; ++j)
				{
					Entity e = entities[j];
					const Velocity& v = reg.get<Velocity>(e);
					Position& p = reg.get<Position>(e);
					p.x += v.x * dt;
					p.y += v.y * dt;
				}
			});
	}
};

// --------------------------------------------------------------------------
// Параллельно ТОЛЬКО ищем попадания (чтение). Каждая задача пишет в свой
// results[chunk]. Изменения применяются потом в главном потоке.
class CollisionSystem : public System
{
	Registry& reg;
	GameData& data;
	struct Hit { Entity bullet, target; };

public:
	CollisionSystem(Registry& r, GameData& d) : reg(r), data(d) {}

	void Update(float, ThreadPool& pool, CommandBuffer&) override
	{
		auto bullets = reg.entitiesWith<Bullet, Position>();
		if (bullets.empty()) return;
		auto enemies = reg.entitiesWith<Enemy, Position, Health>();

		Entity player = findPlayer(reg);
		Position playerPos;
		bool hasPlayer = false;
		if (auto* pp = player.isNull() ? nullptr : reg.tryGet<Position>(player))
		{
			playerPos = *pp;
			hasPlayer = reg.has<Health>(player);
		}

		const size_t chunk = 64;
		std::vector<std::vector<Hit>> results(chunk_count(bullets.size(), chunk));
		const float r2 = 400.f;

		// Параллельно только ищем попадания (чтение), каждая задача пишет в свой results[c]
		parallel_for_chunks(pool, bullets.size(), chunk, [&](size_t c, size_t begin, size_t end)
			{
				for (size_t j = begin; j < end; ++j)
				{
					Entity b = bullets[j];
					const Position& bp = reg.get<Position>(b);
					const Bullet& info = reg.get<Bullet>(b);

					if (info.fromPlayer)
					{
						const float bcx = bp.x + 3.f, bcy = bp.y + 3.f; // центр пули 6x6
						for (Entity en : enemies)
						{
							Box eb = boxOf(reg, en);
							if (bcx >= eb.x - 3.f && bcx <= eb.x + eb.w + 3.f &&
								bcy >= eb.y - 3.f && bcy <= eb.y + eb.h + 3.f)
							{
								results[c].push_back({ b, en });
								break;
							}
						}
					}
					else if (hasPlayer)
					{
						float dx = bp.x - playerPos.x, dy = bp.y - playerPos.y;
						if (dx * dx + dy * dy < r2)
							results[c].push_back({ b, player });
					}
				}
			});

		// Применяем в главном потоке
		for (auto& list : results)
			for (auto& h : list)
			{
				if (!reg.alive(h.bullet) || !reg.alive(h.target)) continue;

				// Ракета: вместо обычного урона - взрыв
				if (auto* ex = reg.tryGet<Explosive>(h.bullet))
				{
					const Position& bp = reg.get<Position>(h.bullet);
					bool mine = reg.get<Bullet>(h.bullet).fromPlayer;
					explode(reg, data, bp.x + 3.f, bp.y + 3.f, ex->radius, ex->damage, mine);
					reg.destroyLater(h.bullet);
					continue;
				}
				hasPlayer = isAlivePlayer(reg, player);
				if (reg.has<Enemy>(h.target))
				{
					damageEnemy(reg, data, h.target, 1);
				}
				else if (auto* hp = reg.tryGet<Health>(h.target)) // цель - игрок
				{
					if (hp->hp > 0) hp->hp--;         // строку с destroyLater удалить
				}
				reg.destroyLater(h.bullet);
			}
	}
};

// --------------------------------------------------------------------------
// Пули за пределами экрана удаляются (раньше копились бесконечно).
class BoundsSystem : public System
{
	Registry& reg;
	float w, h, margin = 50.f;
	const float playerSize = 20.f;

public:
	BoundsSystem(Registry& r, float width, float height) : reg(r), w(width), h(height) {}

	void Update(float, ThreadPool&, CommandBuffer&) override
	{
		// игрок не выходит за окно
		reg.each<Player, Position>([&](Entity, Player&, Position& p)
			{
				p.x = std::clamp(p.x, 0.f, w - playerSize);
				p.y = std::clamp(p.y, 0.f, h - playerSize);
			});

		// пули за экраном удаляются
		reg.each<Bullet, Position>([&](Entity e, Bullet&, Position& p)
			{
				if (p.x < -margin || p.x > w + margin || p.y < -margin || p.y > h + margin)
					reg.destroyLater(e);
			});
	}
};

// --------------------------------------------------------------------------
// Враг вплотную к игроку: -1 HP, затем секунда неуязвимости к касаниям
// (иначе 5 врагов снимут всё здоровье за пару кадров).
class ContactDamageSystem : public System
{
	Registry& reg;
	const float cooldown = 1.0f;

public:
	explicit ContactDamageSystem(Registry& r) : reg(r) {}

	void Update(float dt, ThreadPool&, CommandBuffer&) override
	{
		for (Entity player : reg.entitiesWith<Player, Position, Health>())
		{
			if (!reg.has<ContactCooldown>(player)) reg.add<ContactCooldown>(player);

			ContactCooldown& cd = reg.get<ContactCooldown>(player);
			cd.timer -= dt;
			if (cd.timer > 0.f) continue;

			Health& hp = reg.get<Health>(player);
			if (hp.hp <= 0) continue;               // погибший не получает урон

			const Box pb = boxOf(reg, player);

			bool touched = false;
			reg.each<Enemy, Position>([&](Entity e, Enemy&, Position&)
				{
					Box eb = boxOf(reg, e);
					if (pb.x < eb.x + eb.w && pb.x + pb.w > eb.x &&
						pb.y < eb.y + eb.h && pb.y + pb.h > eb.y)
						touched = true;
				});

			if (touched)
			{
				hp.hp--;
				cd.timer = cooldown;
				// шаг 3 заменит удаление на состояние "погиб"
			}
		}
	}
};

// --------------------------------------------------------------------------
// Бесконечные волны. Когда врагов не осталось, через `delay` секунд приходит
// следующая: врагов больше (3 + 2*N, максимум 20), а каждые 3 волны они
// получают +1 HP (максимум 6). В начале каждой новой волны игрок лечится на 1.
// Враги появляются за краем экрана, не рядом с игроком.
class WaveSystem : public System
{
	Registry& reg;
	GameData& data;
	float screenW, screenH;
	std::mt19937 rng{ std::random_device{}() };
	const float delay = 2.0f;
	float timer = 1.0f; // пауза перед первой волной

	struct Spawn { float x, y, shootTimer; EnemyKind kind; };

	float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }

	EnemyKind randomKind(int wave)
	{
		float r = rnd(0.f, 1.f);
		if (wave >= 4 && r < 0.10f) return EnemyKind::Rocketeer;
		if (wave >= 4 && r < 0.20f) return EnemyKind::Flamer;
		if (wave >= 5 && r < 0.35f) return EnemyKind::Tank;
		if (wave >= 3 && r < 0.60f) return EnemyKind::Fast;
		return EnemyKind::Grunt;
	}

	Spawn randomSpawn(const Position& playerPos, int wave)
	{
		Spawn s{};
		s.kind = randomKind(wave);
		for (int attempt = 0; attempt < 10; ++attempt)
		{
			switch (std::uniform_int_distribution<int>(0, 3)(rng))
			{
			case 0:  s.x = rnd(0.f, screenW - 20.f); s.y = -20.f;     break;
			case 1:  s.x = rnd(0.f, screenW - 20.f); s.y = screenH;   break;
			case 2:  s.x = -20.f;    s.y = rnd(0.f, screenH - 20.f);  break;
			default: s.x = screenW;  s.y = rnd(0.f, screenH - 20.f);  break;
			}
			float dx = s.x - playerPos.x, dy = s.y - playerPos.y;
			if (dx * dx + dy * dy > 150.f * 150.f) break;
		}
		s.shootTimer = rnd(1.0f, 2.5f);
		return s;
	}

public:
	WaveSystem(Registry& r, GameData& d, float w, float h)
		: reg(r), data(d), screenW(w), screenH(h) {}

	void Update(float dt, ThreadPool&, CommandBuffer& cmd) override
	{
		Entity player = findPlayer(reg);
		auto* pp = player.isNull() ? nullptr : reg.tryGet<Position>(player);
		if (!pp) return;

		bool anyEnemy = false;
		reg.each<Enemy>([&](Entity, Enemy&) { anyEnemy = true; });
		if (anyEnemy) { timer = delay; return; }

		timer -= dt;
		if (timer > 0.f) return;

		data.wave++;
		timer = delay;

		if (data.wave > 1)
			if (auto* hp = reg.tryGet<Health>(player))
				hp->hp = std::min(hp->hp + 1, hp->max);
		{
			const Position pl = *pp;
			int wallCount = std::uniform_int_distribution<int>(3, 6)(rng);
			auto rects = generateWalls(screenW, screenH, pl.x, pl.y, wallCount);
			cmd.add([rects](Registry& r) { replaceWalls(r, rects); });
		}
		const bool bossWave = data.wave % 5 == 0;
		int count = bossWave ? 2 + data.wave / 5 : std::min(3 + 2 * data.wave, 20); // на боссе свита поменьше
		int baseHp = std::min(3 + (data.wave - 1) / 3, 6);

		std::vector<Spawn> spawns;
		for (int i = 0; i < count; ++i)
			spawns.push_back(randomSpawn(*pp, data.wave));

		if (bossWave)
		{
			Spawn boss{};
			boss.kind = EnemyKind::Boss;
			boss.x = screenW / 2.f - 20.f; // по центру сверху, выезжает из-за края
			boss.y = -50.f;
			spawns.push_back(boss);
		}

		cmd.add([spawns, baseHp, wave = data.wave](Registry& r)
			{
				for (const Spawn& s : spawns)
				{
					if (s.kind == EnemyKind::Boss)
					{
						int bossHp = 30 + 15 * (wave / 5 - 1); // 30, 45, 60...
						Entity b = r.create();
						r.add<Position>(b, s.x, s.y);
						r.add<Velocity>(b);
						r.add<Enemy>(b, EnemyKind::Boss);
						r.add<Health>(b, bossHp, bossHp);
						r.add<Collider>(b, 40.f, 40.f);
						r.add<BossAI>(b);
						continue;
					}

					int hpBonus = s.kind == EnemyKind::Fast ? -1 : s.kind == EnemyKind::Tank ? 2 : 0;
					int hp = std::max(1, baseHp + hpBonus);
					float cd = s.kind == EnemyKind::Rocketeer ? 3.5f
						: s.kind == EnemyKind::Flamer ? 0.7f
						: 1.5f;

					Entity e = r.create();
					r.add<Position>(e, s.x, s.y);
					r.add<Velocity>(e);
					r.add<Enemy>(e, s.kind);
					r.add<Shooter>(e, cd, s.shootTimer);
					r.add<Health>(e, hp, hp);
					r.add<Collider>(e);
				}
			});
	}
};

// --------------------------------------------------------------------------
// Толкает игрока и врагов (у кого есть Collider) наружу из стен по оси
// с наименьшим перекрытием (MTV). Стены статичны, поэтому их не двигаем.
class WallCollisionSystem : public System
{
	Registry& reg;

public:
	explicit WallCollisionSystem(Registry& r) : reg(r) {}

	void Update(float, ThreadPool&, CommandBuffer&) override
	{
		auto walls = reg.entitiesWith<Wall, Position>();
		if (walls.empty()) return;

		reg.each<Collider, Position>([&](Entity, Collider& col, Position& pos)
			{
				for (Entity w : walls)
				{
					const Position& wp = reg.get<Position>(w);
					const Wall& wall = reg.get<Wall>(w);

					float overlapX = std::min(pos.x + col.w, wp.x + wall.w) - std::max(pos.x, wp.x);
					float overlapY = std::min(pos.y + col.h, wp.y + wall.h) - std::max(pos.y, wp.y);
					if (overlapX <= 0.f || overlapY <= 0.f) continue;

					float ecx = pos.x + col.w / 2.f, ecy = pos.y + col.h / 2.f;
					float wcx = wp.x + wall.w / 2.f, wcy = wp.y + wall.h / 2.f;

					if (overlapX < overlapY)
						pos.x += (ecx < wcx) ? -overlapX : overlapX;
					else
						pos.y += (ecy < wcy) ? -overlapY : overlapY;
				}
			});
	}
};

// --------------------------------------------------------------------------
// Пули (свои и вражеские) уничтожаются при попадании в стену - укрытие
// от них защищает.
class BulletWallSystem : public System
{
	Registry& reg;
	GameData& data;
	const float bulletSize = 6.f;

public:
	BulletWallSystem(Registry& r, GameData& d) : reg(r), data(d) {}

	void Update(float, ThreadPool&, CommandBuffer&) override
	{
		auto walls = reg.entitiesWith<Wall, Position>();
		if (walls.empty()) return;

		struct Boom { float x, y, radius; int dmg; bool fromPlayer; };
		std::vector<Boom> booms; // взрываем после цикла, чтобы не менять реестр внутри each

		reg.each<Bullet, Position>([&](Entity e, Bullet& bl, Position& p)
			{
				for (Entity w : walls)
				{
					const Position& wp = reg.get<Position>(w);
					const Wall& wall = reg.get<Wall>(w);
					if (p.x < wp.x + wall.w && p.x + bulletSize > wp.x &&
						p.y < wp.y + wall.h && p.y + bulletSize > wp.y)
					{
						if (auto* ex = reg.tryGet<Explosive>(e))
							booms.push_back({ p.x + bulletSize / 2.f, p.y + bulletSize / 2.f,
											  ex->radius, ex->damage, bl.fromPlayer });
						reg.destroyLater(e);
						break;
					}
				}
			});

		for (auto& b : booms)
			explode(reg, data, b.x, b.y, b.radius, b.dmg, b.fromPlayer);
	}
};

class ExplosionSystem : public System
{
	Registry& reg;

public:
	explicit ExplosionSystem(Registry& r) : reg(r) {}

	void Update(float dt, ThreadPool&, CommandBuffer&) override
	{
		reg.each<Explosion>([&](Entity e, Explosion& x)
			{
				x.life -= dt;
				if (x.life <= 0.f) reg.destroyLater(e);
			});
	}
};

class BossSystem : public System
{
	Registry& reg;

	struct Shot { float x, y, vx, vy; bool rocket; };

public:
	explicit BossSystem(Registry& r) : reg(r) {}

	void Update(float dt, ThreadPool&, CommandBuffer& cmd) override
	{
		Entity player = findPlayer(reg);
		auto* pp = player.isNull() ? nullptr : reg.tryGet<Position>(player);
		if (!pp) return;
		const float tx = pp->x + 10.f, ty = pp->y + 10.f;

		for (Entity e : reg.entitiesWith<BossAI, Position, Velocity, Health>())
		{
			BossAI& ai = reg.get<BossAI>(e);
			Velocity& vel = reg.get<Velocity>(e);
			const Health& hp = reg.get<Health>(e);
			const Box b = boxOf(reg, e);

			float dx = tx - b.cx(), dy = ty - b.cy();
			float len = std::sqrt(dx * dx + dy * dy);
			if (len < 1.f) { vel.x = vel.y = 0.f; continue; }
			dx /= len;
			dy /= len;

			const bool enraged = hp.hp * 2 <= hp.max; // вторая фаза

			// Сближается, пока дальше 200 px, потом стоит и стреляет
			const float speed = enraged ? 60.f : 35.f;
			const float chase = len > 200.f ? 1.f : 0.f;
			vel.x = dx * speed * chase;
			vel.y = dy * speed * chase;

			ai.fireTimer -= dt;
			ai.rocketTimer -= dt;

			std::vector<Shot> shots;
			const float baseAngle = std::atan2(dy, dx);
			const float muzzle = b.w / 2.f + 10.f; // чтобы пули не рождались внутри босса

			if (ai.fireTimer <= 0.f)
			{
				ai.fireTimer = enraged ? 1.3f : 2.0f;
				const int pellets = enraged ? 7 : 5;
				const float spread = enraged ? 1.0f : 0.7f; // ширина веера в радианах
				for (int i = 0; i < pellets; ++i)
				{
					float t = (float)i / (pellets - 1) - 0.5f;
					float a = baseAngle + t * spread;
					float cs = std::cos(a), sn = std::sin(a);
					shots.push_back({ b.cx() + cs * muzzle - 3.f, b.cy() + sn * muzzle - 3.f,
									  cs * 170.f, sn * 170.f, false });
				}
			}

			if (enraged && ai.rocketTimer <= 0.f)
			{
				ai.rocketTimer = 3.5f;
				shots.push_back({ b.cx() + dx * muzzle - 3.f, b.cy() + dy * muzzle - 3.f,
								  dx * 110.f, dy * 110.f, true });
			}

			if (!shots.empty())
			{
				cmd.add([shots](Registry& r)
					{
						for (const Shot& s : shots)
						{
							Entity bl = r.create();
							r.add<Position>(bl, s.x, s.y);
							r.add<Velocity>(bl, s.vx, s.vy);
							r.add<Bullet>(bl, false);
							if (s.rocket) r.add<Explosive>(bl, 60.f, 1);
						}
					});
			}
		}
	}
};

class FlameSystem : public System
{
	Registry& reg;
public:
	explicit FlameSystem(Registry& r) : reg(r) {}
	void Update(float dt, ThreadPool&, CommandBuffer&) override
	{
		reg.each<Flame>([&](Entity e, Flame& f)
			{
				f.life -= dt;
				if (f.life <= 0.f) reg.destroyLater(e);
			});
	}
};