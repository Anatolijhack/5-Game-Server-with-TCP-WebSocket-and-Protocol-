#include "SessionRepository.h"
#include <openssl/rand.h>
#include <stdexcept>
#include <iostream>

static void print_odbc_error(SQLSMALLINT handle_type, SQLHANDLE handle)
{
    SQLCHAR sqlstate[6];
    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER native_error;
    SQLSMALLINT message_len;
    SQLSMALLINT i = 1;

    while (SQLGetDiagRecA(handle_type, handle, i, sqlstate, &native_error,
        message, sizeof(message), &message_len) == SQL_SUCCESS)
    {
        std::cerr << "ODBC Error [" << sqlstate << "] " << message << std::endl;
        i++;
    }
}

int SessionRepository::remove_expired()
{
    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    SQLHSTMT stmt = nullptr;

    SQLRETURN result =
        SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    if (!SQL_SUCCEEDED(result))
        throw std::runtime_error(
            "Failed to allocate statement handle"
        );

    int idle_sec = static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(
            idle_ttl
        ).count()
        );

    int absolute_sec = static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(
            absolute_ttl
        ).count()
        );

    const char* query =
        "DELETE FROM Sessions "
        "WHERE DATEDIFF(SECOND, last_used_at, GETDATE()) >= ? "
        "   OR DATEDIFF(SECOND, created_at, GETDATE()) >= ?";

    result = SQLPrepareA(
        stmt,
        (SQLCHAR*)query,
        SQL_NTS
    );

    if (!SQL_SUCCEEDED(result))
    {
        print_odbc_error(SQL_HANDLE_STMT, stmt);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);

        throw std::runtime_error(
            "Failed to prepare session cleanup"
        );
    }

    SQLLEN idle_ind = 0;
    SQLLEN absolute_ind = 0;

    SQLBindParameter(
        stmt,
        1,
        SQL_PARAM_INPUT,
        SQL_C_LONG,
        SQL_INTEGER,
        0,
        0,
        &idle_sec,
        0,
        &idle_ind
    );

    SQLBindParameter(
        stmt,
        2,
        SQL_PARAM_INPUT,
        SQL_C_LONG,
        SQL_INTEGER,
        0,
        0,
        &absolute_sec,
        0,
        &absolute_ind
    );

    result = SQLExecute(stmt);

    if (!SQL_SUCCEEDED(result))
    {
        print_odbc_error(SQL_HANDLE_STMT, stmt);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);

        throw std::runtime_error(
            "Failed to delete expired sessions"
        );
    }

    SQLLEN rows = 0;
    SQLRowCount(stmt, &rows);

    SQLEndTran(
        SQL_HANDLE_DBC,
        dbc,
        SQL_COMMIT
    );

    SQLFreeHandle(
        SQL_HANDLE_STMT,
        stmt
    );

    return static_cast<int>(rows);
}

std::string SessionRepository::generate_token()
{
    unsigned char bytes[32];

    if (RAND_bytes(bytes, sizeof(bytes)) != 1)
        throw std::runtime_error("RAND_bytes failed");

    static const char* hex = "0123456789abcdef";
    std::string token;
    token.reserve(64);

    for (unsigned char b : bytes)
    {
        token.push_back(hex[b >> 4]);
        token.push_back(hex[b & 0x0F]);
    }

    return token;
}

std::string SessionRepository::create_token(int user_id, const std::string& username, const std::string& role)
{
    std::string token = generate_token();

    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    SQLHSTMT stmt = nullptr;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    const char* query =
        "INSERT INTO Sessions (token, user_id, username, role) VALUES (?, ?, ?, ?)";
    SQLPrepareA(stmt, (SQLCHAR*)query, SQL_NTS);

    SQLLEN token_ind = SQL_NTS;
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        token.size(), 0, (SQLPOINTER)token.c_str(), 0, &token_ind);

    SQLLEN uid_ind = 0;
    SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &user_id, 0, &uid_ind);

    SQLLEN uname_ind = SQL_NTS;
    SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        username.size(), 0, (SQLPOINTER)username.c_str(), 0, &uname_ind);

    SQLLEN role_ind = SQL_NTS;
    SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        role.size(), 0, (SQLPOINTER)role.c_str(), 0, &role_ind);

    SQLRETURN result = SQLExecute(stmt);

    if (!SQL_SUCCEEDED(result))
    {
        print_odbc_error(SQL_HANDLE_STMT, stmt);
    }

    SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_COMMIT);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);

    return token;
}

std::optional<AuthSession> SessionRepository::find(const std::string& token)
{
    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    long long idle_sec = std::chrono::duration_cast<std::chrono::seconds>(idle_ttl).count();
    long long abs_sec = std::chrono::duration_cast<std::chrono::seconds>(absolute_ttl).count();

    // Один запрос: находим живую сессию и сразу удаляем просроченную, если она такой оказалась
    SQLHSTMT check_stmt = nullptr;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &check_stmt);

    const char* query =
        "SELECT user_id, username, role "
        "FROM Sessions "
        "WHERE token = ? "
        "  AND DATEDIFF(SECOND, last_used_at, GETDATE()) < ? "
        "  AND DATEDIFF(SECOND, created_at, GETDATE()) < ?";

    SQLPrepareA(check_stmt, (SQLCHAR*)query, SQL_NTS);

    SQLLEN token_ind = SQL_NTS;
    SQLBindParameter(check_stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        token.size(), 0, (SQLPOINTER)token.c_str(), 0, &token_ind);

    int idle_sec_i = static_cast<int>(idle_sec);
    int abs_sec_i = static_cast<int>(abs_sec);
    SQLLEN idle_ind = 0, abs_ind = 0;
    SQLBindParameter(check_stmt, 2, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &idle_sec_i, 0, &idle_ind);
    SQLBindParameter(check_stmt, 3, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &abs_sec_i, 0, &abs_ind);

    SQLExecute(check_stmt);

    SQLINTEGER user_id = 0;
    SQLCHAR username[64] = {}, role[32] = {};
    SQLLEN uid_ind = 0, uname_ind = 0, role_ind = 0;

    SQLBindCol(check_stmt, 1, SQL_C_LONG, &user_id, 0, &uid_ind);
    SQLBindCol(check_stmt, 2, SQL_C_CHAR, username, sizeof(username), &uname_ind);
    SQLBindCol(check_stmt, 3, SQL_C_CHAR, role, sizeof(role), &role_ind);

    bool found = SQL_SUCCEEDED(SQLFetch(check_stmt));
    SQLFreeHandle(SQL_HANDLE_STMT, check_stmt);

    if (!found)
    {
        return std::nullopt;
    }

    // Продлеваем скользящее окно
    SQLHSTMT update_stmt = nullptr;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &update_stmt);

    const char* update_query = "UPDATE Sessions SET last_used_at = GETDATE() WHERE token = ?";
    SQLPrepareA(update_stmt, (SQLCHAR*)update_query, SQL_NTS);

    SQLLEN upd_token_ind = SQL_NTS;
    SQLBindParameter(update_stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        token.size(), 0, (SQLPOINTER)token.c_str(), 0, &upd_token_ind);

    SQLExecute(update_stmt);
    SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_COMMIT);
    SQLFreeHandle(SQL_HANDLE_STMT, update_stmt);

    return AuthSession{ static_cast<int>(user_id), std::string((char*)username), std::string((char*)role) };
}

bool SessionRepository::remove(const std::string& token)
{
    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    SQLHSTMT stmt = nullptr;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    const char* query = "DELETE FROM Sessions WHERE token = ?";
    SQLPrepareA(stmt, (SQLCHAR*)query, SQL_NTS);

    SQLLEN token_ind = SQL_NTS;
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        token.size(), 0, (SQLPOINTER)token.c_str(), 0, &token_ind);

    SQLRETURN result = SQLExecute(stmt);

    SQLLEN rows = 0;
    if (result != SQL_NO_DATA && SQL_SUCCEEDED(result)) SQLRowCount(stmt, &rows);

    SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_COMMIT);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);

    return rows > 0;
}

size_t SessionRepository::remove_all_for_user(int user_id)
{
    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    SQLHSTMT stmt = nullptr;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    const char* query = "DELETE FROM Sessions WHERE user_id = ?";
    SQLPrepareA(stmt, (SQLCHAR*)query, SQL_NTS);

    SQLLEN uid_ind = 0;
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &user_id, 0, &uid_ind);

    SQLRETURN result = SQLExecute(stmt);

    SQLLEN rows = 0;
    if (result != SQL_NO_DATA && SQL_SUCCEEDED(result)) SQLRowCount(stmt, &rows);

    SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_COMMIT);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);

    return static_cast<size_t>(rows);
}