#include "LoginThrottleRepository.h"
#include <algorithm>
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

ThrottleStatus LoginThrottleRepository::check(const std::string& username)
{
    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    SQLHSTMT stmt = nullptr;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    // DATEDIFF возвращает секунды до blocked_until пр€мо на стороне SQL Server,
    // так не нужно синхронизировать часы приложени€ и сервера Ѕƒ
    const char* query =
        "SELECT DATEDIFF(SECOND, GETDATE(), blocked_until) "
        "FROM LoginAttempts "
        "WHERE username = ? AND blocked_until IS NOT NULL AND blocked_until > GETDATE()";

    SQLPrepareA(stmt, (SQLCHAR*)query, SQL_NTS);

    SQLLEN name_ind = SQL_NTS;
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        username.size(), 0, (SQLPOINTER)username.c_str(), 0, &name_ind);

    SQLExecute(stmt);

    SQLINTEGER seconds_left = 0;
    SQLLEN sec_ind = 0;
    SQLBindCol(stmt, 1, SQL_C_LONG, &seconds_left, 0, &sec_ind);

    bool found = SQL_SUCCEEDED(SQLFetch(stmt));
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);

    if (!found)
    {
        return { false, 0 };
    }

    return { true, static_cast<int>(seconds_left) + 1 };
}

void LoginThrottleRepository::record_failure(const std::string& username)
{
    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    // MERGE: если строки дл€ этого username ещЄ нет Ч создаЄм со счЄтчиком 1,
    // если есть Ч увеличиваем failures и пересчитываем блокировку в одном атомарном запросе
    SQLHSTMT stmt = nullptr;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    const char* query =
        "MERGE LoginAttempts AS target "
        "USING (SELECT ? AS username) AS src "
        "ON target.username = src.username "
        "WHEN MATCHED THEN UPDATE SET "
        "    failures = target.failures + 1, "
        "    blocked_until = CASE "
        "        WHEN target.failures + 1 > 2 "
        "        THEN DATEADD(SECOND, "
        "            IIF(POWER(2, LEAST(target.failures + 1 - 2, 8)) > 300, 300, "
        "                POWER(2, LEAST(target.failures + 1 - 2, 8))), "
        "            GETDATE()) "
        "        ELSE NULL "
        "    END "
        "WHEN NOT MATCHED THEN "
        "    INSERT (username, failures, blocked_until) VALUES (src.username, 1, NULL);";

    SQLPrepareA(stmt, (SQLCHAR*)query, SQL_NTS);

    SQLLEN name_ind = SQL_NTS;
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        username.size(), 0, (SQLPOINTER)username.c_str(), 0, &name_ind);

    SQLRETURN result = SQLExecute(stmt);

    if (!SQL_SUCCEEDED(result) && result != SQL_NO_DATA)
    {
        print_odbc_error(SQL_HANDLE_STMT, stmt);
    }

    SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_COMMIT);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
}

void LoginThrottleRepository::record_success(const std::string& username)
{
    auto leased = pool.acquire();
    SQLHDBC dbc = leased.handle();

    SQLHSTMT stmt = nullptr;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    const char* query = "DELETE FROM LoginAttempts WHERE username = ?";
    SQLPrepareA(stmt, (SQLCHAR*)query, SQL_NTS);

    SQLLEN name_ind = SQL_NTS;
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
        username.size(), 0, (SQLPOINTER)username.c_str(), 0, &name_ind);

    SQLExecute(stmt);
    SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_COMMIT);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
}