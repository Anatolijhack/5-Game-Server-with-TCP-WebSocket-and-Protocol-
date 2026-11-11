#pragma once
#include "UserReposiory.h"
#include "SessionRepository.h"
#include "ThreadPool.h"
#include <memory>
#include <nlohmann/json.hpp>

#include "Session.h"
#include "Chathub.h"


using json = nlohmann::json;
class Session;
struct AttachmentMeta
{
    std::string name;
    int duration_ms = 0;
};
class ChatService
{
public:
    struct TokenIdentity { int user_id; std::string username; std::string role; };
    bool verify_token(const std::string& token,
        std::function<void(std::optional<TokenIdentity>)> done);
    ChatService(ConnectionPool& pool, SessionRepository& sessions, ThreadPool& workers,
        std::vector<std::string> allowed_origins = {})
        : users(pool), sessions(sessions), workers(workers),
        allowed_origins(std::move(allowed_origins)) {}

    
    std::string dump(const json& j);

    void drop_stream(const std::string& id);
    void sse_open(const std::shared_ptr<Session>& s, const std::string& token);
    std::shared_ptr<Session> find_stream(const std::string& id);
    void poll_open(const std::shared_ptr<Session>& http, const std::string& token);

private:
    using json = nlohmann::json;







    ChatHub hub;
    UserRepository users;
    SessionRepository& sessions;
    ThreadPool& workers;
    std::mutex streams_m;
    std::unordered_map<std::string, std::weak_ptr<Session>> streams;
    std::unordered_map<std::string, std::shared_ptr<Session>> devices;

    std::vector<std::string> allowed_origins;
};