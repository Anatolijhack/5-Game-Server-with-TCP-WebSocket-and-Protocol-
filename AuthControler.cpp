#include "AuthControler.h"
#include <nlohmann/json.hpp>
#include "PasswordHash.h" 
#include "AuthMiddleware.h"
#include "Validator.h"
using json = nlohmann::json;

void AuthController::register_routes(Router& router)
{
    router.add("POST", "/login", [this](const Request& req)
        {
            return login(req);
        }
    );

    router.add("POST", "/register", [this](const Request& req)
        {
            return register_user(req);
        }
    );

    router.add("POST", "/setup/create-admin", [this](const Request& req)
        {
            return create_admin_setup(req);
        }
    );
    router.add("POST", "/setup/create-worker", [this](const Request& req) {return create_worker_setup(req); });

    router.add("POST", "/logout", [this](const Request& req) { return logout(req); });
    router.add("POST", "/logout/all", [this](const Request& req) { return logout_all(req); });
    router.add("PUT", "/me/password", [this](const Request& req)
        {
            return change_password(req);
        });
    router.add("GET", "/admin/users", [this](const Request& req) { return list_users(req); });
    router.add("PUT", "/admin/users/:id/role", [this](const Request& req) { return change_user_role(req); });


}

//Response AuthController::login(const Request& req)
//{
//    json body;
//
//    try
//    {
//        body = json::parse(req.body);
//    }
//    catch (...)
//    {
//        return Response{ R"({"error":"Invalid JSON"})", "application/json", "400 Bad Request" };
//    }
//
//    std::string username, password;
//
//    try
//    {
//        username = body.at("username").get<std::string>();
//        password = body.at("password").get<std::string>();
//    }
//    catch (...)
//    {
//        return Response{ R"({"error":"Missing username or password"})", "application/json", "400 Bad Request" };
//    }
//
//    int retry_after = 0;
//
//    if (LoginThrottle::instance().is_blocked(username, retry_after))
//    {
//        json err = { {"error", "Too many failed attempts"}, {"retry_after_seconds", retry_after} };
//        return Response{ err.dump(), "application/json", "429 Too Many Requests" };
//    }
//
//    auto user = user_repository.find_by_username(username);
//
//    if (!user || !PasswordHash::verify_password(password, user->password_hash))
//    {
//        LoginThrottle::instance().record_failure(username);
//        return Response{ R"({"error":"Invalid credentials"})", "application/json", "401 Unauthorized" };
//    }
//
//    LoginThrottle::instance().record_success(username);
//
//    std::string token = SessionStore::instance().create_token(user->id, user->username, user->role);
//
//    json response = {
//        {"token", token},
//        {"role", user->role},
//        {"user_id", user->id},
//        {"expires_in", SessionStore::instance().idle_seconds()}
//    };
//
//    return Response{ response.dump(), "application/json" };
//}
Response AuthController::login(const Request& req)
{
    json body;

    try { body = json::parse(req.body); }
    catch (...) { return Response{ R"({"error":"Invalid JSON"})", "application/json", "400 Bad Request" }; }

    std::string username, password;

    try
    {
        username = body.at("username").get<std::string>();
        password = body.at("password").get<std::string>();
    }
    catch (...)
    {
        return Response{ R"({"error":"Missing username or password"})", "application/json", "400 Bad Request" };
    }

    auto throttle = throttle_repository.check(username);

    if (throttle.blocked)
    {
        json err = { {"error", "Too many failed attempts"}, {"retry_after_seconds", throttle.retry_after_seconds} };
        return Response{ err.dump(), "application/json", "429 Too Many Requests" };
    }

    auto user = user_repository.find_by_username(username);

    if (!user || !PasswordHash::verify_password(password, user->password_hash))
    {
        throttle_repository.record_failure(username);
        return Response{ R"({"error":"Invalid credentials"})", "application/json", "401 Unauthorized" };
    }

    throttle_repository.record_success(username);

    std::string token = sessions.create_token(user->id, user->username, user->role);

    json response = {
        {"token", token},
        {"role", user->role},
        {"user_id", user->id},
        {"expires_in",sessions.idle_seconds()}
    };

    return Response{ response.dump(), "application/json" };
}



Response AuthController::register_user(const Request& req)
{
    json body;

    try
    {
        body = json::parse(req.body);
    }
    catch (...)
    {
        return Response{ R"({"error":"Invalid JSON"})", "application/json", "400 Bad Request" };
    }

    std::string username, password;
    std::string role = "customer"; // фиксированна€ роль дл€ саморегистрации

    try
    {
        username = body.at("username").get<std::string>();
        password = body.at("password").get<std::string>();
    }
    catch (...)
    {
        return Response{ R"({"error":"Missing username or password"})", "application/json", "400 Bad Request" };
    }

    if (auto error = validation::validate_username(username))
    {
        json err = { {"error", error->message}, {"field", error->field} };
        return Response{ err.dump(), "application/json", "400 Bad Request" };
    }

    if (auto error = validation::validate_password(password))
    {
        json err = { {"error", error->message}, {"field", error->field} };
        return Response{ err.dump(), "application/json", "400 Bad Request" };
    }

    std::string password_hash = PasswordHash::hash_password(password);

    std::string result = user_repository.create_user(username, password_hash, role);
    json result_json = json::parse(result);

    std::string status_code = "201 Created";
    if (result_json.contains("error"))
    {
        status_code = result_json["error"] == "Username already exists" ? "409 Conflict" : "400 Bad Request";
    }

    return Response{ result_json.dump(), "application/json", status_code };
}
Response AuthController::create_admin_setup(const Request& req)
{
    json body;

    try
    {
        body = json::parse(req.body);
    }
    catch (...)
    {
        return Response{ R"({"error":"Invalid JSON"})", "application/json", "400 Bad Request" };
    }

    std::string username, password;

    try
    {
        username = body.at("username").get<std::string>();
        password = body.at("password").get<std::string>();
    }
    catch (...)
    {
        return Response{ R"({"error":"Missing username or password"})", "application/json", "400 Bad Request" };
    }

    if (auto error = validation::validate_username(username))
    {
        json err = { {"error", error->message}, {"field", error->field} };
        return Response{ err.dump(), "application/json", "400 Bad Request" };
    }

    if (auto error = validation::validate_password(password))
    {
        json err = { {"error", error->message}, {"field", error->field} };
        return Response{ err.dump(), "application/json", "400 Bad Request" };
    }

    std::string password_hash = PasswordHash::hash_password(password);

    std::string result = user_repository.create_user(username, password_hash, "admin");
    json result_json = json::parse(result);

    std::string status_code = "201 Created";
    if (result_json.contains("error"))
    {
        status_code = result_json["error"] == "Username already exists" ? "409 Conflict" : "400 Bad Request";
    }

    return Response{ result_json.dump(), "application/json", status_code };
}
Response AuthController::create_worker_setup(const Request& req)
{
    json body;

    try
    {
        body = json::parse(req.body);
    }
    catch (...)
    {
        return Response{ R"({"error":"Invalid JSON"})", "application/json", "400 Bad Request" };
    }

    std::string username, password;

    try
    {
        username = body.at("username").get<std::string>();
        password = body.at("password").get<std::string>();
    }
    catch (...)
    {
        return Response{ R"({"error":"Missing username or password"})", "application/json", "400 Bad Request" };
    }

    if (auto error = validation::validate_username(username))
    {
        json err = { {"error", error->message}, {"field", error->field} };
        return Response{ err.dump(), "application/json", "400 Bad Request" };
    }

    if (auto error = validation::validate_password(password))
    {
        json err = { {"error", error->message}, {"field", error->field} };
        return Response{ err.dump(), "application/json", "400 Bad Request" };
    }

    std::string password_hash = PasswordHash::hash_password(password);

    std::string result = user_repository.create_user(username, password_hash, "employee");
    json result_json = json::parse(result);

    std::string status_code = "201 Created";
    if (result_json.contains("error"))
    {
        status_code = result_json["error"] == "Username already exists" ? "409 Conflict" : "400 Bad Request";
    }

    return Response{ result_json.dump(), "application/json", status_code };
}
Response AuthController::logout(const Request& req)
{
    auto auth = require_auth(req, sessions);
    if (!auth.ok) return auth.error;

    sessions.remove(*bearer_token(req));

    return Response{ R"({"status":"logged out"})", "application/json" };
}

Response AuthController::logout_all(const Request& req)
{
    auto auth = require_auth(req, sessions);
    if (!auth.ok) return auth.error;

    size_t closed = sessions.remove_all_for_user(auth.user_id);
    

    json response = { {"status", "logged out everywhere"}, {"sessions_closed", closed} };
    return Response{ response.dump(), "application/json" };
}
Response AuthController::change_password(const Request& req)
{
    auto auth = require_auth(req, sessions);
    if (!auth.ok) return auth.error;

    json body;

    try
    {
        body = json::parse(req.body);
    }
    catch (...)
    {
        return Response{ R"({"error":"Invalid JSON"})", "application/json", "400 Bad Request" };
    }

    std::string current_password, new_password;

    try
    {
        current_password = body.at("current_password").get<std::string>();
        new_password = body.at("new_password").get<std::string>();
    }
    catch (...)
    {
        return Response{ R"({"error":"Missing current_password or new_password"})", "application/json", "400 Bad Request" };
    }

    if (auto error = validation::validate_password(new_password))
    {
        json err = { {"error", error->message}, {"field", error->field} };
        return Response{ err.dump(), "application/json", "400 Bad Request" };
    }

    auto user = user_repository.find_by_id(auth.user_id);

    if (!user)
    {
        return Response{ R"({"error":"User not found"})", "application/json", "404 Not Found" };
    }

    if (!PasswordHash::verify_password(current_password, user->password_hash))
    {
        return Response{ R"({"error":"Current password is incorrect"})", "application/json", "401 Unauthorized" };
    }

    if (current_password == new_password)
    {
        return Response{ R"({"error":"New password must differ from current password"})", "application/json", "400 Bad Request" };
    }

    std::string new_hash = PasswordHash::hash_password(new_password);
    std::string result = user_repository.update_password(auth.user_id, new_hash);
    json result_json = json::parse(result);

    if (result_json.contains("error"))
    {
        return Response{ result_json.dump(), "application/json", "500 Internal Server Error" };
    }

    // ќтзываем все токены Ч придЄтс€ войти заново с новым паролем
    sessions.remove_all_for_user(auth.user_id);

    return Response{ R"({"status":"password updated, please log in again"})", "application/json" };
}

Response AuthController::list_users(const Request& req)
{
    auto auth = require_roles(req, sessions, { "admin" });
    if (!auth.ok) return auth.error;

    return Response{ user_repository.get_all_users(), "application/json" };
}

Response AuthController::change_user_role(const Request& req)
{
    auto auth = require_roles(req, sessions, { "admin" });
    if (!auth.ok) return auth.error;

    int target_user_id;

    try
    {
        target_user_id = std::stoi(req.params.at("id"));
    }
    catch (...)
    {
        return Response{ R"({"error":"Invalid user id"})", "application/json", "400 Bad Request" };
    }

    json body;

    try
    {
        body = json::parse(req.body);
    }
    catch (...)
    {
        return Response{ R"({"error":"Invalid JSON"})", "application/json", "400 Bad Request" };
    }

    std::string new_role;

    try
    {
        new_role = body.at("role").get<std::string>();
    }
    catch (...)
    {
        return Response{ R"({"error":"Missing role field"})", "application/json", "400 Bad Request" };
    }

    static const std::vector<std::string> allowed_roles = { "admin", "employee", "seller", "customer" };

    if (std::find(allowed_roles.begin(), allowed_roles.end(), new_role) == allowed_roles.end())
    {
        return Response{ R"({"error":"Invalid role"})", "application/json", "400 Bad Request" };
    }

    if (target_user_id == auth.user_id)
    {
        return Response{ R"({"error":"Cannot change your own role"})", "application/json", "400 Bad Request" };
    }

    std::string result = user_repository.update_user_role(target_user_id, new_role);
    json result_json = json::parse(result);

    std::string status_code = "200 OK";

    if (result_json.contains("error"))
    {
        status_code = (result_json["error"] == "User not found") ? "404 Not Found" : "500 Internal Server Error";
    }
    else
    {
        // роль сменилась Ч старые токены с прежними правами больше не должны работать
        sessions.remove_all_for_user(target_user_id);
    }

    return Response{ result_json.dump(), "application/json", status_code };
}
