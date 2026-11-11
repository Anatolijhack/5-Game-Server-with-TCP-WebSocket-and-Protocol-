#pragma once
#include "SessionRepository.h"
#include "Logger.h"
#include <atomic>
#include <chrono>
#include <thread>

class SessionCleaner
{
public:
    SessionCleaner(SessionRepository& repo)
        : repo(repo),
        worker([this] { run(); })
    {
    }

    ~SessionCleaner()
    {
        stop = true;

        if (worker.joinable())
            worker.join();
    }

private:
    void run()
    {
        while (!stop)
        {
            try
            {
                int n = repo.remove_expired();

                if (n > 0)
                {
                    LOG_INFO(
                        "Removed expired sessions: "
                        + std::to_string(n)
                    );
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR(
                    std::string("Session cleaner failed: ")
                    + e.what()
                );
            }

            for (int i = 0; i < 60 && !stop; i++)
            {
                std::this_thread::sleep_for(
                    std::chrono::seconds(1)
                );
            }
        }
    }

    SessionRepository& repo;

    std::atomic<bool> stop{ false };

    std::thread worker;   // îáúÿâëåí ïîñëåäíèì
};