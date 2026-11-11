#include "ConectionPool.h"
#include <iostream>

ConnectionPool::ConnectionPool(size_t size, const std::string& conn_str)
    : connection_string(conn_str), pool_size(size)
{
    for (size_t i = 0; i < size; i++)
    {
        DatabaseConnection conn = create_connection();

        if (conn.dbc == nullptr)
        {
            throw std::runtime_error("Failed to initialize connection pool: connection " + std::to_string(i));
        }

        available.push(std::move(conn));
    }

    std::cerr << "ConnectionPool: initialized with " << size << " connections" << std::endl;
}

DatabaseConnection ConnectionPool::create_connection()
{
    DatabaseConnection connection;
    SQLRETURN result;

    result = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &connection.env);
    if (!SQL_SUCCEEDED(result)) return {};

    SQLSetEnvAttr(connection.env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);

    result = SQLAllocHandle(SQL_HANDLE_DBC, connection.env, &connection.dbc);
    if (!SQL_SUCCEEDED(result))
    {
        SQLFreeHandle(SQL_HANDLE_ENV, connection.env);
        return {};
    }

    SQLCHAR conn_str_buf[512];
    strncpy_s((char*)conn_str_buf, sizeof(conn_str_buf), connection_string.c_str(), _TRUNCATE);

    result = SQLDriverConnectA(
        connection.dbc, nullptr, conn_str_buf, SQL_NTS,
        nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT
    );

    if (!SQL_SUCCEEDED(result))
    {
        SQLFreeHandle(SQL_HANDLE_DBC, connection.dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, connection.env);
        return {};
    }

    return connection;
}

ConnectionPool::LeasedConnection ConnectionPool::acquire()
{
    std::unique_lock<std::mutex> lock(mtx);

    cv.wait(lock, [this]() { return !available.empty(); });

    DatabaseConnection conn = std::move(available.front());
    available.pop();

    return LeasedConnection(*this, std::move(conn));
}

void ConnectionPool::release(DatabaseConnection conn)
{
    std::lock_guard<std::mutex> lock(mtx);
    available.push(std::move(conn));
    cv.notify_one();
}

ConnectionPool::~ConnectionPool()
{
    while (!available.empty())
    {
        DatabaseConnection conn = std::move(available.front());
        available.pop();

        if (conn.dbc)
        {
            SQLFreeHandle(SQL_HANDLE_DBC, conn.dbc);
        }
        if (conn.env)
        {
            SQLFreeHandle(SQL_HANDLE_ENV, conn.env);
        }
    }
}
