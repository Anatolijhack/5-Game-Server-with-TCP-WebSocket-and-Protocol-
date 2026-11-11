#pragma once
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <algorithm>

class LoginThrottle
{
public:
    static LoginThrottle& instance()
    {
        static LoginThrottle throttle;
        return throttle;
    }

    // Можно ли сейчас пробовать логиниться под этим именем
    bool is_blocked(const std::string& username, int& retry_after_seconds)
    {
        std::lock_guard<std::mutex> lock(mtx);

        auto it = attempts.find(username);
        if (it == attempts.end())
            return false;

        auto now = Clock::now();

        if (now < it->second.blocked_until)
        {
            retry_after_seconds = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>(it->second.blocked_until - now).count()) + 1;
            return true;
        }

        return false;
    }

    void record_failure(const std::string& username)
    {
        std::lock_guard<std::mutex> lock(mtx);

        auto& entry = attempts[username];
        entry.failures++;

        // 0-2 попытки без задержки, дальше экспоненциальный рост: 2с, 4с, 8с... максимум 5 минут
        if (entry.failures > 2)
        {
            int power = (std::min)(entry.failures - 2, 8); // ограничиваем степень, чтобы не переполнить
            int delay_seconds = (std::min)(300, 1 << power);
            entry.blocked_until = Clock::now() + std::chrono::seconds(delay_seconds);
        }
    }

    void record_success(const std::string& username)
    {
        std::lock_guard<std::mutex> lock(mtx);
        attempts.erase(username);
    }

private:
    using Clock = std::chrono::steady_clock;

    struct Entry
    {
        int failures = 0;
        Clock::time_point blocked_until = Clock::now();
    };

    std::mutex mtx;
    std::unordered_map<std::string, Entry> attempts;
};