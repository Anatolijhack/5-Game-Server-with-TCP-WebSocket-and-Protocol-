#pragma once
#include "Structs.h"
#include "SessionRepository.h"
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

struct AuthCheckResult
{
    bool ok;
    int user_id;
    std::string username;
    std::string role;
    Response error;
};

inline std::optional<std::string> bearer_token(const Request& req)
{
    auto it = req.headers.find("authorization");

    if (it == req.headers.end() || it->second.rfind("Bearer ", 0) != 0)
        return std::nullopt;

    return it->second.substr(7);
}

inline AuthCheckResult authenticate(const Request& req, SessionRepository& sessions)
{
    auto token = bearer_token(req);

    if (!token)
    {
        return { false, 0, "", "", Response{ R"({"error":"Missing Authorization header"})", "application/json", "401 Unauthorized" } };
    }

    auto session = sessions.find(*token);

    if (!session)
    {
        return { false, 0, "", "", Response{ R"({"error":"Invalid or expired token"})", "application/json", "401 Unauthorized" } };
    }

    return { true, session->user_id, session->username, session->role, {} };
}

inline AuthCheckResult require_roles(const Request& req, SessionRepository& sessions, const std::vector<std::string>& allowed)
{
    auto auth = authenticate(req, sessions);
    if (!auth.ok) return auth;

    if (std::find(allowed.begin(), allowed.end(), auth.role) == allowed.end())
    {
        return { false, 0, "", "", Response{ R"({"error":"Insufficient permissions"})", "application/json", "403 Forbidden" } };
    }

    return auth;
}

inline AuthCheckResult require_auth(const Request& req, SessionRepository& sessions, const std::string& required_role = "")
{
    if (required_role.empty()) return authenticate(req, sessions);
    return require_roles(req, sessions, { required_role });
}