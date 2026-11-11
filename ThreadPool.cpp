#include "ThreadPool.h"
#include <random>


void ThreadPool::DoWork(int id)
{
	local_queue = queues[id].get();

	while (true)
	{
		std::function<void()> task = local_queue->pop(stop);

		// если pop вернул пусто → либо stop, либо украсть
		if (!task)
		{
			if (stop.load())
				break;

			// 🔥 random steal (уменьшает contention)
			thread_local std::mt19937 rng(std::random_device{}());
			std::uniform_int_distribution<size_t> dist(0, queues.size() - 1);

			size_t start = dist(rng);

			for (size_t i = 0; i < queues.size(); i++)
			{
				size_t victim = (start + i) % queues.size();
				if (victim == id) continue;

				task = queues[victim]->steal();
				if (task) break;
			}
		}

		if (task)
		{
			try { task(); }
			catch (...) {}
			continue;
		}

		// 🔥 избегаем жёсткого sleep
		std::this_thread::yield();

		std::unique_lock<std::mutex> lock(mtx2);
		cv2.wait(lock, [this]() {
			return stop.load() || task_count.load() > 0;
			});

		if (stop && task_count.load() == 0)
			break;
	}
}
ThreadPool::ThreadPool(int size)
{
	queues.resize(size);
	for (int i = 0; i < size; i++)
	{
		queues[i] = std::make_unique<WorkQueue>();
	}
	for (int i = 0; i < size; i++)
	{
		workers.emplace_back([this, i]() {DoWork(i); });
	}
}
void ThreadPool::push_task(std::function<void()> task, int priority)
{
	{
		std::unique_lock<std::mutex> lock(mtx2);

		cv2.wait(lock, [this]() {
			return stop.load() || task_count.load() < 100;
			});

		if (stop.load())
			return;

		task_count++;
	}

	auto wrapper = [task = std::move(task), this]() mutable {
		try { task(); }
		catch (...) {}

		if (task_count.fetch_sub(1) == 1)
			cv2.notify_all();
		};

	if (local_queue)
	{
		local_queue->push(std::move(wrapper), priority);
	}
	else
	{
		static std::atomic<size_t> index = 0;
		size_t i = index++ % queues.size();
		queues[i]->push(std::move(wrapper), priority);
	}

	cv2.notify_all(); // 🔥 было notify_one
}

void ThreadPool::wait()
{
	std::unique_lock<std::mutex> lock(mtx2);
	cv2.wait(lock, [this]() {
		return task_count.load() == 0;
		});
}
void ThreadPool::shutdown()
{
	{
		std::lock_guard<std::mutex> lock(mtx2);
		stop = true;
	}

	// 🔥 корректное пробуждение всех pop()
	for (auto& q : queues)
	{
		q->notify_all();
	}

	cv2.notify_all();
	wait();

	for (auto& t : workers)
	{
		if (t.joinable())
			t.join();
	}
}

bool ThreadPool::try_push_task(std::function<void()> task, int priority)
{
	{
		std::lock_guard<std::mutex> lock(mtx2);
		if (stop.load() || task_count.load() >= 100) return false;
		task_count++;
	}

	auto wrapper = [task = std::move(task), this]() mutable {
		try { task(); }
		catch (...) {}
		if (task_count.fetch_sub(1) == 1) cv2.notify_all();
		};

	if (local_queue) local_queue->push(std::move(wrapper), priority);
	else
	{
		static std::atomic<size_t> index = 0;
		queues[index++ % queues.size()]->push(std::move(wrapper), priority);
	}

	cv2.notify_all();
	return true;
}
ThreadPool::~ThreadPool()
{
	shutdown();
}