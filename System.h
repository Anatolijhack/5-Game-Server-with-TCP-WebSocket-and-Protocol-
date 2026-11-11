#pragma once
#include <algorithm>
#include <memory>
#include <vector>
#include "ThreadPool.h"
#include "Registry.h"
#include "Components.h"

class System
{
public:
	virtual ~System() = default;
	virtual void Init() {}
	virtual void Start() {}
	virtual void Stop() {}
	virtual void Update(float dt, ThreadPool& pool, CommandBuffer& cmd) {}
	virtual void Render(float dt) {}
	virtual void SetInput(const InputState&) {}
};

// f(chunkIndex, begin, end). ∆дЄт завершени€ всех кусков перед возвратом,
// поэтому f можно захватывать по ссылке.
template <class Func>
inline void parallel_for_chunks(ThreadPool& pool, size_t count, size_t chunk, Func&& f)
{
	size_t c = 0;
	for (size_t i = 0; i < count; i += chunk, ++c)
	{
		size_t end = std::min(i + chunk, count);
		pool.submit(0, [&f, c, i, end]() { f(c, i, end); });
	}
	pool.wait();
}

inline size_t chunk_count(size_t count, size_t chunk) { return (count + chunk - 1) / chunk; }

// —истемы выполн€ютс€ строго по пор€дку. ѕосле каждой примен€етс€ буфер команд
// и удал€ютс€ отложенные сущности, так что следующа€ система видит чистый мир.
class ModuleRegistry
{
	std::vector<std::shared_ptr<System>> modules;
	ThreadPool pool{ 4 };
	CommandBuffer cmd;
	Registry& reg;

public:
	explicit ModuleRegistry(Registry& r) : reg(r) {}

	void push_module(std::shared_ptr<System> m) { modules.push_back(std::move(m)); }
	void SetInput(const InputState& in) { for (auto& m : modules) m->SetInput(in); }
	void Init() { for (auto& m : modules) m->Init(); }
	void Start() { for (auto& m : modules) m->Start(); }
	void Stop() { for (auto& m : modules) m->Stop(); }

	void Update(float dt)
	{
		for (auto& m : modules)
		{
			m->Update(dt, pool, cmd);
			cmd.apply(reg);
			reg.flush();
		}
	}
	void Render(float dt) { for (auto& m : modules) m->Render(dt); }
};