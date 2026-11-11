#include "UserReposiory.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
static void print_odbc_error(SQLSMALLINT handle_type, SQLHANDLE handle)
{
    SQLCHAR sqlstate[6];
    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER native_error;
    SQLSMALLINT message_len;
    SQLSMALLINT i = 1;

    while (SQLGetDiagRecA(
        handle_type,
        handle,
        i,
        sqlstate,
        &native_error,
        message,
        sizeof(message),
        &message_len
    ) == SQL_SUCCESS)
    {
        std::cerr << "ODBC Error [" << sqlstate << "] " << message << std::endl;
        i++;
    }
}

std::optional<UserRecord> UserRepository::find_by_username(const std::string& username)
{
    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    SQLHSTMT stmt = nullptr;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    const char* query =
        "SELECT id, login, hash_password, role "
        "FROM Users "
        "WHERE login = ?";
    SQLPrepareA(stmt, (SQLCHAR*)query, SQL_NTS);

    SQLLEN name_len = SQL_NTS;
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        username.size(), 0, (SQLPOINTER)username.c_str(), 0, &name_len);

    SQLRETURN exec_result = SQLExecute(stmt);

    if (!SQL_SUCCEEDED(exec_result))
    {
        print_odbc_error(SQL_HANDLE_STMT, stmt);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return std::nullopt;
    }

    SQLINTEGER out_id = 0;                                                   // новое
    SQLCHAR out_username[64] = {}, out_hash[256] = {}, out_role[32] = {};
    SQLLEN id_ind = 0, u_ind = 0, p_ind = 0, r_ind = 0;                      // + id_ind

    SQLBindCol(stmt, 1, SQL_C_LONG, &out_id, 0, &id_ind);  // id
    SQLBindCol(stmt, 2, SQL_C_CHAR, out_username, sizeof(out_username), &u_ind);   // login
    SQLBindCol(stmt, 3, SQL_C_CHAR, out_hash, sizeof(out_hash), &p_ind);   // hash_password
    SQLBindCol(stmt, 4, SQL_C_CHAR, out_role, sizeof(out_role), &r_ind);   // role

    std::optional<UserRecord> result;

    if (SQL_SUCCEEDED(SQLFetch(stmt)))                                       // было == SQL_SUCCESS
    {
        result = UserRecord{
            static_cast<int>(out_id),                                        // id первым
            std::string((char*)out_username),
            std::string((char*)out_hash),
            std::string((char*)out_role)
        };
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return result;
}
std::string UserRepository::create_user(const std::string& username, const std::string& password_hash, const std::string& role)
{
    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    SQLHSTMT stmt = nullptr;
    SQLRETURN result = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    if (!SQL_SUCCEEDED(result))
    {
        return R"({"error":"Failed to allocate statement handle"})";
    }

    const char* query =
        "INSERT INTO Users (login, hash_password, role) "
        "VALUES (?, ?, ?)";

    result = SQLPrepareA(stmt, (SQLCHAR*)query, SQL_NTS);

    if (!SQL_SUCCEEDED(result))
    {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return R"({"error":"Failed to prepare statement"})";
    }

    SQLLEN username_len = SQL_NTS;
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        username.size(), 0, (SQLPOINTER)username.c_str(), 0, &username_len);

    SQLLEN hash_len = SQL_NTS;
    SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        password_hash.size(), 0, (SQLPOINTER)password_hash.c_str(), 0, &hash_len);

    SQLLEN role_len = SQL_NTS;
    SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        role.size(), 0, (SQLPOINTER)role.c_str(), 0, &role_len);

    result = SQLExecute(stmt);

    if (!SQL_SUCCEEDED(result))
    {
        SQLCHAR sqlstate[6];
        SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
        SQLINTEGER native_error;
        SQLSMALLINT message_len;

        SQLGetDiagRecA(SQL_HANDLE_STMT, stmt, 1, sqlstate, &native_error, message, sizeof(message), &message_len);

        std::string sqlstate_str((char*)sqlstate);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);

        if (sqlstate_str == "23000") // нарушение уникальности
        {
            return R"({"error":"Username already exists"})";
        }

        return R"({"error":"Failed to create user"})";
    }

    result = SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_COMMIT);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);

    if (!SQL_SUCCEEDED(result))
    {
        return R"({"error":"Commit failed"})";
    }

    return R"({"status":"user created"})";
}
std::optional<UserRecord> UserRepository::find_by_id(int user_id)
{
    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    SQLHSTMT stmt = nullptr;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    const char* query = "SELECT id, login, hash_password, role FROM Users WHERE id = ?";
    SQLPrepareA(stmt, (SQLCHAR*)query, SQL_NTS);

    SQLLEN id_ind = 0;
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &user_id, 0, &id_ind);

    SQLRETURN exec_result = SQLExecute(stmt);

    if (!SQL_SUCCEEDED(exec_result))
    {
        print_odbc_error(SQL_HANDLE_STMT, stmt);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return std::nullopt;
    }

    SQLINTEGER out_id = 0;
    SQLCHAR out_username[64] = {}, out_hash[256] = {}, out_role[32] = {};
    SQLLEN id_out_ind = 0, u_ind = 0, h_ind = 0, r_ind = 0;

    SQLBindCol(stmt, 1, SQL_C_LONG, &out_id, 0, &id_out_ind);
    SQLBindCol(stmt, 2, SQL_C_CHAR, out_username, sizeof(out_username), &u_ind);
    SQLBindCol(stmt, 3, SQL_C_CHAR, out_hash, sizeof(out_hash), &h_ind);
    SQLBindCol(stmt, 4, SQL_C_CHAR, out_role, sizeof(out_role), &r_ind);

    std::optional<UserRecord> result;

    if (SQL_SUCCEEDED(SQLFetch(stmt)))
    {
        result = UserRecord{
            static_cast<int>(out_id),
            std::string((char*)out_username),
            std::string((char*)out_hash),
            std::string((char*)out_role)
        };
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return result;
}

std::string UserRepository::update_password(int user_id, const std::string& new_password_hash)
{
    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    SQLHSTMT stmt = nullptr;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    const char* query = "UPDATE Users SET hash_password = ? WHERE id = ?";
    SQLPrepareA(stmt, (SQLCHAR*)query, SQL_NTS);

    SQLLEN hash_len = SQL_NTS;
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        new_password_hash.size(), 0, (SQLPOINTER)new_password_hash.c_str(), 0, &hash_len);

    SQLLEN id_ind = 0;
    SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &user_id, 0, &id_ind);

    SQLRETURN result = SQLExecute(stmt);

    if (!SQL_SUCCEEDED(result) && result != SQL_NO_DATA)
    {
        print_odbc_error(SQL_HANDLE_STMT, stmt);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return R"({"error":"Update failed"})";
    }

    SQLLEN rows = 0;
    if (result != SQL_NO_DATA)
    {
        SQLRowCount(stmt, &rows);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);

    if (rows == 0)
    {
        return R"({"error":"User not found"})";
    }

    return R"({"status":"password updated"})";
}

// UserRepository.cpp
std::string UserRepository::get_all_users()
{
    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    SQLHSTMT stmt = nullptr;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    // hash_password сюда не попадает Ч пароли наружу не отдаЄм даже в хешированном виде
    const char* query = "SELECT id, login, role FROM Users ORDER BY id";
    SQLExecDirectA(stmt, (SQLCHAR*)query, SQL_NTS);

    SQLINTEGER id = 0;
    SQLCHAR login[64] = {}, role[32] = {};
    SQLLEN id_ind, login_ind, role_ind;

    SQLBindCol(stmt, 1, SQL_C_LONG, &id, 0, &id_ind);
    SQLBindCol(stmt, 2, SQL_C_CHAR, login, sizeof(login), &login_ind);
    SQLBindCol(stmt, 3, SQL_C_CHAR, role, sizeof(role), &role_ind);

    json users = json::array();

    while (SQLFetch(stmt) == SQL_SUCCESS)
    {
        json item;
        item["id"] = id;
        item["login"] = std::string((char*)login);
        item["role"] = std::string((char*)role);
        users.push_back(item);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return users.dump();
}

std::string UserRepository::update_user_role(int user_id, const std::string& new_role)
{
    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    SQLHSTMT stmt = nullptr;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    const char* query = "UPDATE Users SET role = ? WHERE id = ?";
    SQLPrepareA(stmt, (SQLCHAR*)query, SQL_NTS);

    SQLLEN role_len = SQL_NTS;
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        new_role.size(), 0, (SQLPOINTER)new_role.c_str(), 0, &role_len);

    SQLLEN id_ind = 0;
    SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &user_id, 0, &id_ind);

    SQLRETURN result = SQLExecute(stmt);

    if (!SQL_SUCCEEDED(result) && result != SQL_NO_DATA)
    {
        print_odbc_error(SQL_HANDLE_STMT, stmt);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return R"({"error":"Update failed"})";
    }

    SQLLEN rows = 0;
    if (result != SQL_NO_DATA) SQLRowCount(stmt, &rows);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);

    if (rows == 0)
    {
        return R"({"error":"User not found"})";
    }

    return R"({"status":"role updated"})";
}
