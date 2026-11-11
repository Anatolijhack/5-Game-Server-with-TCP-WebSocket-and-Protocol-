#include "ChatService.h"
static std::string random_hex(size_t bytes)
{
    std::string raw(bytes, '\0');
    RAND_bytes(reinterpret_cast<unsigned char*>(&raw[0]), static_cast<int>(bytes));
    static const char* d = "0123456789abcdef";
    std::string out;
    for (unsigned char c : raw) { out += d[c >> 4]; out += d[c & 15]; }
    return out;
}
std::string ChatService::dump(const json& j)
{
    // replace: битый UTF-8 не бросит исключение, как раньше с 0xEF
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}
void ChatService::drop_stream(const std::string& id)
{
    std::shared_ptr<Session> keep;                 // освободим объект уже после снятия мьютекса
    {
        std::lock_guard<std::mutex> lk(streams_m);
        streams.erase(id);
        auto d = devices.find(id);
        if (d != devices.end()) { keep = std::move(d->second); devices.erase(d); }
    }
}

void ChatService::sse_open(const std::shared_ptr<Session>& s, const std::string& token)
{
    bool queued = workers.try_push_task([this, s, token]()
        {
            auto session = sessions.find(token);

            s->ws_post([this, s, session]()
                {
                    if (!session) { s->reject_http("401 Unauthorized"); return; }

                    bool first = false;
                    if (!hub.attach(session->user_id, s, &first))
                    {
                        s->reject_http("429 Too Many Requests");
                        return;
                    }

                    s->chat_identity = Session::ChatIdentity{ session->user_id, session->username, session->role };

                    const std::string id = random_hex(16);
                    s->stream_id = id;
                    {
                        std::lock_guard<std::mutex> lk(streams_m);
                        streams[id] = s;
                    }

                    s->sse_begin();
                    s->ws_send(dump({ {"type", "auth_ok"}, {"login", session->username}, {"stream", id} }));
                    
                });
        }, 0);

    if (!queued) s->reject_http("503 Service Unavailable");
}

std::shared_ptr<Session> ChatService::find_stream(const std::string& id)
{
    std::lock_guard<std::mutex> lk(streams_m);
    auto it = streams.find(id);
    if (it != streams.end()) return it->second.lock();
    auto d = devices.find(id);
    return d == devices.end() ? nullptr : d->second;
}

void ChatService::poll_open(const std::shared_ptr<Session>& http, const std::string& token)
{
    bool queued = workers.try_push_task([this, http, token]()
        {
            auto session = sessions.find(token);

            http->ws_post([this, http, session]()
                {
                    if (!session) { http->reject_http("401 Unauthorized"); return; }

                    auto dev = Session::make_device(*http);

                    bool first = false;
                    if (!hub.attach(session->user_id, dev, &first))
                    {
                        http->reject_http("429 Too Many Requests");
                        return;
                    }

                    dev->chat_identity = Session::ChatIdentity{ session->user_id, session->username, session->role };
                    dev->device_id = random_hex(16);
                    {
                        std::lock_guard<std::mutex> lk(streams_m);
                        devices[dev->device_id] = dev;
                    }

                    dev->arm_timer(60s);
                    dev->ws_send(dump({ {"type", "auth_ok"}, {"login", session->username},
                                        {"device", dev->device_id} }));
                    

                    dev->ws_post([dev, http]() { dev->poll_take(http, 0); });   // первый ответ с накопленным
                });
        }, 0);

    if (!queued) http->reject_http("503 Service Unavailable");
}

bool ChatService::verify_token(const std::string& token,
    std::function<void(std::optional<TokenIdentity>)> done)
{
    return workers.try_push_task([this, token, done = std::move(done)]()
        {
            auto session = sessions.find(token);          // БД: в рабочем потоке
            if (!session) { done(std::nullopt); return; }
            done(TokenIdentity{ session->user_id, session->username, session->role });
        }, 0);
}
