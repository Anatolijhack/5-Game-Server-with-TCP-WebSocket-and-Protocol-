#pragma once
#include <windows.h>  
#include <sql.h>
#include <sqlext.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <stdexcept>
#include "Structs.h" // где у вас определена структура DatabaseConnection
#include <string>

class ConnectionPool
{
public:
    ConnectionPool(size_t size, const std::string& connection_string);
    ~ConnectionPool();

    // RAII-обёртка: соединение автоматически возвращается в пул при уничтожении
    class LeasedConnection
    {
    public:
        LeasedConnection(ConnectionPool& pool, DatabaseConnection conn)
            : pool(pool), conn(std::move(conn)) {}

        ~LeasedConnection()
        {
            if (conn.dbc != nullptr)
            {
                pool.release(std::move(conn));
            }
        }

        // Запрещаем копирование, разрешаем перемещение
        LeasedConnection(const LeasedConnection&) = delete;
        LeasedConnection& operator=(const LeasedConnection&) = delete;

        LeasedConnection(LeasedConnection&& other) noexcept
            : pool(other.pool), conn(std::move(other.conn))
        {
            other.conn.dbc = nullptr;
        }

        SQLHDBC handle() const { return conn.dbc; }
        DatabaseConnection& get() { return conn; }

    private:
        ConnectionPool& pool;
        DatabaseConnection conn;
    };

    LeasedConnection acquire();

private:
    void release(DatabaseConnection conn);
    DatabaseConnection create_connection();

    std::string connection_string;
    std::queue<DatabaseConnection> available;
    std::mutex mtx;
    std::condition_variable cv;
    size_t pool_size;
};