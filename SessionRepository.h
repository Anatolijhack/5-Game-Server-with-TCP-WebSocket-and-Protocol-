#pragma once

#include <chrono>
#include <optional>
#include <string>
#include "ConectionPool.h"

struct AuthSession
{
    int user_id;
    std::string username;
    std::string role;
};

class SessionRepository
{
public:
    SessionRepository(ConnectionPool& pool, std::chrono::minutes idle, std::chrono::hours absolute)
        : pool(pool), idle_ttl(idle), absolute_ttl(absolute) {}

    long long idle_seconds() const
    {
        return std::chrono::duration_cast<std::chrono::seconds>(idle_ttl).count();
    }

    std::string create_token(int user_id, const std::string& username, const std::string& role);
    std::optional<AuthSession> find(const std::string& token);
    bool remove(const std::string& token);
    size_t remove_all_for_user(int user_id);
    int remove_expired();
private:
    ConnectionPool& pool;
    std::chrono::minutes idle_ttl;
    std::chrono::hours absolute_ttl;

    static std::string generate_token();
};