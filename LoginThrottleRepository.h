#pragma once
#include "ConectionPool.h"
#include <string>
#include <optional>

struct ThrottleStatus
{
    bool blocked;
    int retry_after_seconds;
};

class LoginThrottleRepository
{
public:
    LoginThrottleRepository(ConnectionPool& pool) : pool(pool) {}

    ThrottleStatus check(const std::string& username);
    void record_failure(const std::string& username);
    void record_success(const std::string& username);

private:
    ConnectionPool& pool;
};