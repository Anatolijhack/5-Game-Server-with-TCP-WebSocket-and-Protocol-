#pragma once
#include <thread>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <future>
#include <memory>
struct Task
{
    int priority = 0;
    int order = 0;
    std::function<void()> f;
    bool operator < (const Task& other) const
    {
        if (priority == other.priority)
        {
            return order > other.order;
        }
        return priority < other.priority;
    }
};
class WorkQueue
{
private:
    std::deque<Task> high;
    std::deque<Task> medium;
    std::deque<Task> low;

    std::mutex mtx;
    std::condition_variable cv_not_empty;
    std::condition_variable cv_not_full;

    size_t max_size = 64;

    size_t size_unsafe() const
    {
        return high.size() + medium.size() + low.size();
    }

public:
    void notify_all()
    {
        cv_not_empty.notify_all();
    }
    void notify_one()
    {
        cv_not_empty.notify_one();
    }
    void push(std::function<void()> f, int priority = 0)
    {
        std::unique_lock<std::mutex> lock(mtx);

        cv_not_full.wait(lock, [&]() {
            return size_unsafe() < max_size;
            });


        Task t;
        t.priority = priority;
        t.f = std::move(f);


        if (priority > 5)
            high.push_back(std::move(t));
        else if (priority > 0)
            medium.push_back(std::move(t));
        else
            low.push_back(std::move(t));

        lock.unlock();
        cv_not_empty.notify_one();
    }

    std::function<void()> pop(std::atomic<bool>& stop)
    {
        std::unique_lock<std::mutex> lock(mtx);

        cv_not_empty.wait(lock, [&]() {
            return stop.load() || size_unsafe() > 0;
            });

        if (stop && size_unsafe() == 0)
            return {};

        Task t;

        if (!high.empty())
        {
            t = std::move(high.front());
            high.pop_front();
        }
        else if (!medium.empty())
        {
            t = std::move(medium.front());
            medium.pop_front();
        }
        else
        {
            t = std::move(low.front());
            low.pop_front();
        }

        lock.unlock();
        cv_not_full.notify_one();

        return std::move(t.f);
    }

    std::function<void()> steal()
    {
        std::lock_guard<std::mutex> lock(mtx);

        Task t;

        if (!low.empty())
        {
            t = std::move(low.back());
            low.pop_back();
        }
        else if (!medium.empty())
        {
            t = std::move(medium.back());
            medium.pop_back();
        }
        else if (!high.empty())
        {
            t = std::move(high.back());
            high.pop_back();
        }
        else
        {
            return {};
        }

        cv_not_full.notify_one();


        return std::move(t.f);
    }
};
class ThreadPool;
template<typename In, typename Out>
class State : public std::enable_shared_from_this<State<In, Out>>
{
private:
    std::function<Out(In)> func;
    std::function<void(Out)> nextStage;
    ThreadPool& pool;

    std::mutex mtx;
    std::condition_variable cv;
    size_t inFlight = 0;
    size_t maxInFlight = 10;

public:
    State(ThreadPool& p, std::function<Out(In)> f)
        : pool(p), func(f) {}

    void setNext(std::function<void(Out)> f)
    {
        nextStage = f;
    }

    std::future<Out> push(In value)
    {
        auto self = this->shared_from_this();

        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&]() { return inFlight < maxInFlight; });
            inFlight++;
        }

        return pool.submit([self, value = std::move(value)]() mutable
            {
                Out current = self->func(std::move(value));

                if (self->nextStage)
                {
                    self->pool.submit(0, self->nextStage, current);
                }

                {
                    std::lock_guard<std::mutex> lock(self->mtx);
                    self->inFlight--;
                }
                self->cv.notify_one();
                return current;
            });
    }
};
class ThreadPool
{
private:
    std::mutex mtx;
    std::mutex mtx2;
    std::condition_variable cv;
    std::condition_variable cv2;
    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<WorkQueue>> queues;
    std::queue<std::function<void()>> tasks;
    inline static thread_local WorkQueue* local_queue = nullptr;
    std::atomic<bool> stop = false;
    std::atomic<int> works;
    std::atomic<int> task_count;
    void DoWork(int id);
public:
    ThreadPool(int size);
    void push_task(std::function<void()> task, int priority);
    void shutdown();
    void wait();
    template<typename F, typename... Args>
    auto submit(int priority, F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using R = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<R()>>(
            [func = std::forward<F>(f),
            tuple = std::make_tuple(std::forward<Args>(args)...)]() mutable
            {
                return std::apply(std::move(func), std::move(tuple));
            }
        );

        std::future<R> result = task->get_future();


        {
            std::unique_lock<std::mutex> lock(mtx2);
            cv2.wait(lock, [this]() {
                return stop || task_count.load() < 100;
                });

            if (stop)
                throw std::runtime_error("ThreadPool stopped");

            task_count++;
        }

        auto wrapper = [task, this]() {
            try {
                (*task)();
            }
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

        cv2.notify_one();

        return result;
    }
    bool try_push_task(std::function<void()> task, int priority = 0);
    ~ThreadPool();
};