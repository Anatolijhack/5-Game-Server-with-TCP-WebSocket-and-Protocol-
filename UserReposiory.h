#pragma once
#include <string>
#include <optional>
#include "ConectionPool.h"

struct UserRecord
{
    int id;
    std::string username;
    std::string password_hash;;
    std::string role;

};

class UserRepository
{
public:
    UserRepository(ConnectionPool& pool) : pool(pool) {}

    std::optional<UserRecord> find_by_username(const std::string& username);
    std::string create_user(const std::string& username, const std::string& password_hash, const std::string& role);
    std::optional<UserRecord> find_by_id(int user_id);
    std::string update_password(int user_id, const std::string& new_password_hash);
    std::string get_all_users();
    std::string update_user_role(int user_id, const std::string& new_role);

private:
    ConnectionPool& pool;
};