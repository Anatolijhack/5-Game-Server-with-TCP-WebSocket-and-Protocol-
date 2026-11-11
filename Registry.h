#pragma once
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>
#include <mutex>
#include <tuple>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Entity = индекс + поколение. Если индекс переиспользован, старый handle
// перестаёт быть "живым" (alive() вернёт false).
// ---------------------------------------------------------------------------
struct Entity
{
	static constexpr uint32_t NullIndex = 0xFFFFFFFFu;
	uint32_t index = NullIndex;
	uint32_t generation = 0;

	bool isNull() const { return index == NullIndex; }
	bool operator==(const Entity& o) const { return index == o.index && generation == o.generation; }
	bool operator!=(const Entity& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Sparse set: sparse[entityIndex] -> позиция в dense; dense/data лежат подряд.
// Удаление = swap-and-pop, порядок не сохраняется.
// ---------------------------------------------------------------------------
struct IPool
{
	virtual ~IPool() = default;
	virtual void remove(uint32_t index) = 0;
	virtual void clear() = 0;
};

template <class T>
class SparseSet : public IPool
{
	static constexpr uint32_t npos = 0xFFFFFFFFu;
	std::vector<uint32_t> sparse; // индекс сущности -> позиция в dense
	std::vector<uint32_t> dense;  // позиция -> индекс сущности
	std::vector<T> data;          // позиция -> компонент

public:
	bool has(uint32_t index) const { return index < sparse.size() && sparse[index] != npos; }
	size_t size() const { return dense.size(); }
	uint32_t indexAt(size_t k) const { return dense[k]; }
	T& dataAt(size_t k) { return data[k]; }

	// Не меняет структуру -> безопасно вызывать из нескольких потоков,
	// пока никто не делает emplace/remove.
	T& get(uint32_t index) { return data[sparse[index]]; }

	template <class... A>
	T& emplace(uint32_t index, A&&... args)
	{
		if (index >= sparse.size())
			sparse.resize(index + 1, npos);

		if (sparse[index] != npos)
		{
			data[sparse[index]] = T{ std::forward<A>(args)... };
			return data[sparse[index]];
		}
		sparse[index] = (uint32_t)dense.size();
		dense.push_back(index);
		data.push_back(T{ std::forward<A>(args)... });
		return data.back();
	}

	void clear() override
	{
		sparse.clear();
		dense.clear();
		data.clear();
	}

	void remove(uint32_t index) override
	{
		if (!has(index)) return;
		uint32_t pos = sparse[index];
		uint32_t last = dense.back();

		dense[pos] = last;
		data[pos] = std::move(data.back());
		sparse[last] = pos;

		dense.pop_back();
		data.pop_back();
		sparse[index] = npos; // после sparse[last], чтобы работал случай index == last
	}

};

// ---------------------------------------------------------------------------
// Registry.
// ПРАВИЛА ПОТОКОВ:
//  * create/destroy/add/remove/flush - только из главного потока, когда
//    рабочие задачи не выполняются.
//  * get/tryGet/has/alive/each/entitiesWith - можно из рабочих потоков,
//    пока структура не меняется. Разные задачи должны писать в РАЗНЫХ сущностей.
//  * destroyLater - потокобезопасно (удаление произойдёт в flush()).
// ---------------------------------------------------------------------------
class Registry
{
	std::vector<uint32_t> generations;
	std::vector<uint32_t> freeList;
	std::vector<std::unique_ptr<IPool>> pools;

	std::mutex pendingMtx;
	std::vector<Entity> pending;

	static size_t nextTypeId()
	{
		static std::atomic<size_t> counter{ 0 };
		return counter++;
	}
	template <class T>
	static size_t typeId()
	{
		static const size_t id = nextTypeId();
		return id;
	}

	template <class T>
	SparseSet<T>* tryPool() const
	{
		size_t id = typeId<T>();
		if (id >= pools.size() || !pools[id]) return nullptr;
		return static_cast<SparseSet<T>*>(pools[id].get());
	}
	template <class T>
	SparseSet<T>& pool()
	{
		size_t id = typeId<T>();
		if (id >= pools.size()) pools.resize(id + 1);
		if (!pools[id]) pools[id] = std::make_unique<SparseSet<T>>();
		return *static_cast<SparseSet<T>*>(pools[id].get());
	}

public:
	Entity create()
	{
		if (!freeList.empty())
		{
			uint32_t idx = freeList.back();
			freeList.pop_back();
			return Entity{ idx, generations[idx] };
		}
		generations.push_back(0);
		return Entity{ (uint32_t)generations.size() - 1, 0 };
	}

	bool alive(Entity e) const
	{
		return !e.isNull() && e.index < generations.size() && generations[e.index] == e.generation;
	}

	void destroy(Entity e)
	{
		if (!alive(e)) return;
		for (auto& p : pools)
			if (p) p->remove(e.index);
		generations[e.index]++;
		freeList.push_back(e.index);
	}

	void destroyLater(Entity e)
	{
		std::lock_guard<std::mutex> lock(pendingMtx);
		pending.push_back(e);
	}

	// Двойной destroyLater безопасен: второй раз alive() уже false.
	void flush()
	{
		std::vector<Entity> local;
		{
			std::lock_guard<std::mutex> lock(pendingMtx);
			local.swap(pending);
		}
		for (auto e : local) destroy(e);
	}

	// Удаляет ВСЕ сущности (для рестарта). Старые handle'ы становятся невалидными.
	void clear()
	{
		for (auto& p : pools)
			if (p) p->clear();
		freeList.clear();
		for (size_t i = generations.size(); i-- > 0;)
		{
			generations[i]++;
			freeList.push_back((uint32_t)i);
		}
		std::lock_guard<std::mutex> lock(pendingMtx);
		pending.clear();
	}

	template <class T, class... A>
	T& add(Entity e, A&&... args)
	{
		assert(alive(e));
		return pool<T>().emplace(e.index, std::forward<A>(args)...);
	}

	template <class T>
	void remove(Entity e)
	{
		if (alive(e))
			if (auto* p = tryPool<T>()) p->remove(e.index);
	}

	template <class T>
	bool has(Entity e) const
	{
		if (!alive(e)) return false;
		auto* p = tryPool<T>();
		return p && p->has(e.index);
	}

	// Без проверок: сущность должна быть жива и иметь компонент.
	template <class T>
	T& get(Entity e) const
	{
		return tryPool<T>()->get(e.index);
	}

	template <class T>
	T* tryGet(Entity e) const
	{
		if (!alive(e)) return nullptr;
		auto* p = tryPool<T>();
		if (!p || !p->has(e.index)) return nullptr;
		return &p->get(e.index);
	}

	// f(Entity, T0&, Ts&...). Итерация идёт по dense-массиву T0.
	// Внутри f нельзя добавлять/удалять компоненты и сущности (используй CommandBuffer / destroyLater).
	template <class T0, class... Ts, class F>
	void each(F&& f) const
	{
		auto* p0 = tryPool<T0>();
		if (!p0 || ((tryPool<Ts>() == nullptr) || ...)) return;
		auto rest = std::make_tuple(tryPool<Ts>()...);
		(void)rest;

		for (size_t k = 0; k < p0->size(); ++k)
		{
			uint32_t idx = p0->indexAt(k);
			if ((std::get<SparseSet<Ts>*>(rest)->has(idx) && ...))
				f(Entity{ idx, generations[idx] }, p0->dataAt(k),
					std::get<SparseSet<Ts>*>(rest)->get(idx)...);
		}
	}

	// Снимок списка сущностей - удобно резать на куски для пула потоков.
	template <class T0, class... Ts>
	std::vector<Entity> entitiesWith() const
	{
		std::vector<Entity> out;
		each<T0, Ts...>([&](Entity e, T0&, Ts&...) { out.push_back(e); });
		return out;
	}
};

// ---------------------------------------------------------------------------
// Отложенные СТРУКТУРНЫЕ изменения (создать сущность, добавить компонент...).
// Значения (позиции, скорости) через него писать не нужно - пишем напрямую.
// add() защищён мьютексом, поэтому безопасен и из рабочих потоков.
// ---------------------------------------------------------------------------
class CommandBuffer
{
	std::mutex mtx;
	std::vector<std::function<void(Registry&)>> commands;

public:
	void add(std::function<void(Registry&)> cmd)
	{
		std::lock_guard<std::mutex> lock(mtx);
		commands.push_back(std::move(cmd));
	}

	void apply(Registry& reg)
	{
		std::vector<std::function<void(Registry&)>> local;
		{
			std::lock_guard<std::mutex> lock(mtx);
			local.swap(commands);
		}
		for (auto& c : local) c(reg);
	}
};