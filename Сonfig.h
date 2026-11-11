#pragma once
#include <string>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

struct DatabaseConfig
{
    std::string driver;
    std::string server;
    std::string database;
    bool trusted_connection;
    bool trust_server_certificate;
    int pool_size;

    std::string to_connection_string() const
    {
        std::string conn = "Driver={" + driver + "};"
            "Server=" + server + ";"
            "Database=" + database + ";";

        if (trusted_connection)
        {
            conn += "Trusted_Connection=yes;";
        }

        if (trust_server_certificate)
        {
            conn += "TrustServerCertificate=yes;";
        }

        return conn;
    }
};

struct ServerConfig
{
    int port;
};

// ÍÎÂÀß ÑÒÐÓÊÒÓÐÀ
struct LiqPayConfig
{
    std::string public_key;
    std::string private_key;
    std::string result_url;
    std::string server_url;
};

class Config
{
public:
    ServerConfig server;
    DatabaseConfig database;
    LiqPayConfig liqpay; // <-- äîáàâèòü ïîëå

    static Config load(const std::string& path)
    {
        std::ifstream file(path);

        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open config file: " + path);
        }

        nlohmann::json j;
        file >> j;

        Config config;

        config.server.port = j.at("server").at("port").get<int>();

        config.database.driver = j.at("database").at("driver").get<std::string>();
        config.database.server = j.at("database").at("server").get<std::string>();
        config.database.database = j.at("database").at("database").get<std::string>();
        config.database.trusted_connection = j.at("database").at("trusted_connection").get<bool>();
        config.database.trust_server_certificate = j.at("database").at("trust_server_certificate").get<bool>();
        config.database.pool_size = j.at("database").at("pool_size").get<int>();

        // ÍÎÂÎÅ — ÷èòàåì ñåêöèþ liqpay
        if (j.contains("liqpay"))
        {
            config.liqpay.public_key = j.at("liqpay").value("public_key", "");
            config.liqpay.private_key = j.at("liqpay").value("private_key", "");
            config.liqpay.result_url = j.at("liqpay").value("result_url", "");
            config.liqpay.server_url = j.at("liqpay").value("server_url", "");
        }

        return config;
    }
};