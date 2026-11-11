#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Кадр в сети:  [u32 длина, big-endian][данные]   (рамку делает Session)
// Данные:       [u8 тип][поля, little-endian]
// Предполагается little-endian процессор (x86, ARM).

namespace proto {

    // Должен совпадать с MAX_RAW_FRAME в Session.cpp
    constexpr std::size_t MAX_PACKET = 8192;

    // Версия протокола. Пока только объявлена; проверять её начнём на шаге 8d-B.
    constexpr std::uint8_t PROTOCOL_VERSION = 4;

    // =====================================================================
    //  КАТАЛОГ СООБЩЕНИЙ
    //  Номера не переиспользуются: удалённое сообщение оставляет мёртвый номер.
    //  Группы: 1-9 сессия, 10-19 пати, 20-29 лобби, 30-39 игра.
    //  Класс: R = должно дойти и по порядку, L = может теряться (свежее заменяет старое).
    // =====================================================================

    // ---------- клиент -> сервер ----------
    // сессия
    constexpr std::uint8_t C2S_HELLO = 1;  // R  [u8 версия][токен ASCII, до 128 байт]
    constexpr std::uint8_t C2S_PING = 2;    // L  без полей
    // пати
    constexpr std::uint8_t C2S_CREATE_PARTY = 10;   // R  без полей
    constexpr std::uint8_t C2S_JOIN_PARTY = 11;   // R  [6 байт код]
    constexpr std::uint8_t C2S_LEAVE_PARTY = 12;   // R  без полей
    // лобби
    constexpr std::uint8_t C2S_SET_READY = 20;   // R  [u8 0/1]
    constexpr std::uint8_t C2S_START = 21;   // R  без полей
    constexpr std::uint8_t C2S_KICK = 22;   // R  [u8 номер участника 1..4]
    // игра
    constexpr std::uint8_t C2S_INPUT = 30;   // L  [u32 seq][u8 флаги][u8 клавиши][i16 aimX][i16 aimY]
    //    вне состояния «игра» сервер игнорирует

// ---------- сервер -> клиент ----------
// сессия
    constexpr std::uint8_t S2C_WELCOME = 1;    // R  [u8 версия][u32 id][u8 частота тика][u16 ширина][u16 высота]
    constexpr std::uint8_t S2C_PONG = 2;    // L  без полей
    constexpr std::uint8_t S2C_KICK = 0x7F; // R  [u8 причина], см. KICK_* в GameNet.h (номер не менять)
    // пати
    constexpr std::uint8_t S2C_PARTY_JOINED = 10;   // R  [u8 ваш номер][6 байт код]
    constexpr std::uint8_t S2C_PARTY_UPDATE = 11;   // R  [u8 состояние][u8 секунд отсчёта][u8 лидер][u8 n]
    //       n x { u8 номер, u8 готов, u8 длина имени, имя }
    //    состояние: 0 лобби, 1 отсчёт, 2 игра, 3 итоги
    constexpr std::uint8_t S2C_PARTY_LEFT = 12;   // R  [u8 причина]
    constexpr std::uint8_t S2C_ERROR = 13;   // R  [u8 код]
    // лобби
    constexpr std::uint8_t S2C_GAME_STARTED = 20;   // R  без полей
    constexpr std::uint8_t S2C_GAME_ENDED = 21;   // R  [u16 волна][u32 счёт][u8 n] n x { u8 номер, u16 очки }
    // игра
    constexpr std::uint8_t S2C_SNAPSHOT = 30;   // L  см. ниже

    // ---------- константы ----------
    constexpr std::size_t PARTY_CODE_LEN = 6;

    // Причины PARTY_LEFT
    constexpr std::uint8_t LEFT_BY_USER = 0;
    constexpr std::uint8_t LEFT_CLOSED = 1;
    constexpr std::uint8_t LEFT_KICKED = 2;

    // Коды ERROR (числа не менять)
    constexpr std::uint8_t ERR_ALREADY_IN = 1;   // вы уже в пати
    constexpr std::uint8_t ERR_NOT_IN = 2;   // вы не в пати
    constexpr std::uint8_t ERR_NO_PARTY = 3;   // «нет такой», «заполнена», «забанен», «игра идёт»: не различаем
    constexpr std::uint8_t ERR_LIMIT = 4;   // лимит пати на сервере
    constexpr std::uint8_t ERR_RATE = 5;   // слишком часто
    constexpr std::uint8_t ERR_NOT_LEADER = 6;   // нет прав
    constexpr std::uint8_t ERR_NOT_READY = 7;   // не все готовы
    constexpr std::uint8_t ERR_BAD_TARGET = 8;   // неверная цель кика
    constexpr std::uint8_t ERR_BAD_STATE = 9;   // команда не в том состоянии (зарезервирован)

    // Флаги ввода
    constexpr std::uint8_t IN_LEFT = 1;
    constexpr std::uint8_t IN_RIGHT = 2;
    constexpr std::uint8_t IN_UP = 4;
    constexpr std::uint8_t IN_DOWN = 8;
    constexpr std::uint8_t IN_SHOOT = 16;
    constexpr std::uint8_t IN_RESTART = 32;
    // Клавиши: бит 0..4 = num1..num5 (нажата ли клавиша прямо сейчас)

    // S2C_SNAPSHOT:
    //   [u32 тик][u32 ack_seq]                        заголовок (свой для каждого игрока)
    //   [u8 фаза][u8 бонус A][u8 бонус B][u8 секунд до конца выбора]
    //   [u16 волна][u32 счёт]
    //   [u8 n] n x { u32 номер участника, i16 x, i16 y, u8 hp, u8 max, u8 оружие, u8 флаги, u16 очки }
    //                                                 флаги: бит0 жив, бит1 бонус выбран
    //   [u16 n] n x { u32 id, u8 вид, i16 x, i16 y, u16 hp, u16 max }               враги
    //   [u16 n] n x { u8 флаги, i16 x, i16 y }                                      пули (бит0 от игрока, бит1 ракета)
    //   [u8 n]  n x { i16 x1, i16 y1, i16 x2, i16 y2 }                              лучи
    //   [u8 n]  n x { i16 x, i16 y, u16 радиус, u8 t }                              взрывы
    //   [u16 n] n x { i16 x, i16 y, u8 t }                                          огонь
    //   [u8 n]  n x { i16 x, i16 y, i16 w, i16 h }           
    class Writer {
    public:
        void u8(std::uint8_t v) { b.push_back(v); }

        void u16(std::uint16_t v)
        {
            b.push_back(static_cast<std::uint8_t>(v & 0xFF));
            b.push_back(static_cast<std::uint8_t>(v >> 8));
        }

        void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }

        void u32(std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }

        void f32(float f)
        {
            std::uint32_t v;
            std::memcpy(&v, &f, 4);
            u32(v);
        }

        void bytes(const std::vector<std::uint8_t>& other) { b.insert(b.end(), other.begin(), other.end()); }

        void bytes(const void* p, std::size_t n)
        {
            const auto* s = static_cast<const std::uint8_t*>(p);
            b.insert(b.end(), s, s + n);
        }
        std::vector<std::uint8_t> b;
    };

    // Все чтения проверяют границы и возвращают false, если данных не хватило.
    class Reader {
    public:
        Reader(const std::uint8_t* d, std::size_t n) : p_(d), end_(d + n) {}

        bool bytes(std::uint8_t* out, std::size_t n)
        {
            if (left() < n) return false;
            std::memcpy(out, p_, n);
            p_ += n;
            return true;
        }

        bool u8(std::uint8_t& v)
        {
            if (p_ + 1 > end_) return false;
            v = *p_++;
            return true;
        }

        bool u16(std::uint16_t& v)
        {
            if (p_ + 2 > end_) return false;
            v = static_cast<std::uint16_t>(p_[0] | (p_[1] << 8));
            p_ += 2;
            return true;
        }

        bool i16(std::int16_t& v)
        {
            std::uint16_t t;
            if (!u16(t)) return false;
            v = static_cast<std::int16_t>(t);
            return true;
        }

        bool u32(std::uint32_t& v)
        {
            if (p_ + 4 > end_) return false;
            v = static_cast<std::uint32_t>(p_[0])
                | (static_cast<std::uint32_t>(p_[1]) << 8)
                | (static_cast<std::uint32_t>(p_[2]) << 16)
                | (static_cast<std::uint32_t>(p_[3]) << 24);
            p_ += 4;
            return true;
        }

        bool f32(float& f)
        {
            std::uint32_t v;
            if (!u32(v)) return false;
            std::memcpy(&f, &v, 4);
            return true;
        }

        std::size_t left() const { return static_cast<std::size_t>(end_ - p_); }
        const std::uint8_t* ptr() const { return p_; }

    private:
        const std::uint8_t* p_;
        const std::uint8_t* end_;
    };

    // =====================================================================
//  КОДЕКИ. Декодер получает кадр целиком (с байтом типа), возвращает false
//  при любом нарушении: нет данных, лишние байты, значение вне диапазона.
// =====================================================================

    constexpr std::size_t MAX_NAME_LEN = 16;
    constexpr std::size_t MAX_PARTY_MEMBERS = 4;

    // ---- PartyJoined ----
    struct PartyJoinedMsg
    {
        std::uint8_t num = 0;          // ваш номер 1..4
        std::string  code;             // PARTY_CODE_LEN символов
    };

    inline void encode(Writer& w, const PartyJoinedMsg& m)
    {
        w.u8(S2C_PARTY_JOINED);
        w.u8(m.num);
        w.bytes(m.code.data(), PARTY_CODE_LEN);
    }

    inline bool decode(const std::uint8_t* d, std::size_t n, PartyJoinedMsg& m)
    {
        Reader r(d, n);
        std::uint8_t type = 0;
        char buf[PARTY_CODE_LEN];
        if (!r.u8(type) || type != S2C_PARTY_JOINED) return false;
        if (!r.u8(m.num) || m.num < 1 || m.num > MAX_PARTY_MEMBERS) return false;
        if (!r.bytes(reinterpret_cast<std::uint8_t*>(buf), sizeof buf)) return false;
        m.code.assign(buf, sizeof buf);
        return r.left() == 0;
    }

    // ---- PartyUpdate ----
    struct PartyMemberMsg
    {
        std::uint8_t num = 0;
        bool         ready = false;
        std::string  name;
    };

    struct PartyUpdateMsg
    {
        std::uint8_t state = 0;        // 0 лобби, 1 отсчёт, 2 игра, 3 итоги
        std::uint8_t countdown = 0;
        std::uint8_t leader = 0;
        std::vector<PartyMemberMsg> members;
    };

    inline void encode(Writer& w, const PartyUpdateMsg& m)
    {
        w.u8(S2C_PARTY_UPDATE);
        w.u8(m.state);
        w.u8(m.countdown);
        w.u8(m.leader);
        w.u8(static_cast<std::uint8_t>(m.members.size()));
        for (const auto& mem : m.members)
        {
            const std::size_t len = std::min(mem.name.size(), MAX_NAME_LEN);
            w.u8(mem.num);
            w.u8(mem.ready ? 1 : 0);
            w.u8(static_cast<std::uint8_t>(len));
            w.bytes(mem.name.data(), len);
        }
    }

    inline bool decode(const std::uint8_t* d, std::size_t n, PartyUpdateMsg& m)
    {
        Reader r(d, n);
        std::uint8_t type = 0, cnt = 0;
        if (!r.u8(type) || type != S2C_PARTY_UPDATE) return false;
        if (!r.u8(m.state) || m.state > 3) return false;
        if (!r.u8(m.countdown) || !r.u8(m.leader) || !r.u8(cnt)) return false;
        if (cnt > MAX_PARTY_MEMBERS) return false;

        m.members.clear();
        for (std::uint8_t i = 0; i < cnt; ++i)
        {
            PartyMemberMsg mem;
            std::uint8_t ready = 0, len = 0;
            if (!r.u8(mem.num) || mem.num < 1 || mem.num > MAX_PARTY_MEMBERS) return false;
            if (!r.u8(ready) || ready > 1) return false;
            if (!r.u8(len) || len > MAX_NAME_LEN) return false;
            mem.ready = ready != 0;
            mem.name.resize(len);
            if (len && !r.bytes(reinterpret_cast<std::uint8_t*>(&mem.name[0]), len)) return false;
            m.members.push_back(std::move(mem));
        }
        return r.left() == 0;
    }

    // ---- GameEnded ----
    struct GameEndedMsg
    {
        std::uint16_t wave = 0;
        std::uint32_t score = 0;
        std::vector<std::pair<std::uint8_t, std::uint16_t>> points;   // номер, очки
    };

    inline void encode(Writer& w, const GameEndedMsg& m)
    {
        w.u8(S2C_GAME_ENDED);
        w.u16(m.wave);
        w.u32(m.score);
        w.u8(static_cast<std::uint8_t>(m.points.size()));
        for (const auto& p : m.points) { w.u8(p.first); w.u16(p.second); }
    }

    inline bool decode(const std::uint8_t* d, std::size_t n, GameEndedMsg& m)
    {
        Reader r(d, n);
        std::uint8_t type = 0, cnt = 0;
        if (!r.u8(type) || type != S2C_GAME_ENDED) return false;
        if (!r.u16(m.wave) || !r.u32(m.score) || !r.u8(cnt) || cnt > MAX_PARTY_MEMBERS) return false;
        m.points.clear();
        for (std::uint8_t i = 0; i < cnt; ++i)
        {
            std::uint8_t num = 0; std::uint16_t pts = 0;
            if (!r.u8(num) || num < 1 || num > MAX_PARTY_MEMBERS || !r.u16(pts)) return false;
            m.points.push_back({ num, pts });
        }
        return r.left() == 0;
    }

    // ---- клиент -> сервер ----
    inline bool decode_join_party(const std::uint8_t* d, std::size_t n, std::string& code)
    {
        Reader r(d, n);
        std::uint8_t type = 0;
        char buf[PARTY_CODE_LEN];
        if (!r.u8(type) || type != C2S_JOIN_PARTY) return false;
        if (!r.bytes(reinterpret_cast<std::uint8_t*>(buf), sizeof buf)) return false;
        code.assign(buf, sizeof buf);
        return r.left() == 0;
    }

    inline bool decode_set_ready(const std::uint8_t* d, std::size_t n, bool& ready)
    {
        Reader r(d, n);
        std::uint8_t type = 0, v = 0;
        if (!r.u8(type) || type != C2S_SET_READY || !r.u8(v) || v > 1) return false;
        ready = v != 0;
        return r.left() == 0;
    }

    inline bool decode_kick_member(const std::uint8_t* d, std::size_t n, std::uint8_t& num)
    {
        Reader r(d, n);
        std::uint8_t type = 0;
        if (!r.u8(type) || type != C2S_KICK) return false;
        if (!r.u8(num) || num < 1 || num > MAX_PARTY_MEMBERS) return false;
        return r.left() == 0;
    }
} // namespace proto