#pragma once
#include <string>
#include <vector>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>

class PasswordHash
{
public:
    // Возвращает строку вида "salt_hex:hash_hex"
    static std::string hash_password(const std::string& password)
    {
        unsigned char salt[16];
        RAND_bytes(salt, sizeof(salt));

        unsigned char hash[32];
        PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            salt, sizeof(salt),
            100000, // количество итераций — чем больше, тем медленнее подбор
            EVP_sha256(),
            sizeof(hash), hash
        );

        return to_hex(salt, sizeof(salt)) + ":" + to_hex(hash, sizeof(hash));
    }

    // Сравнивает введённый пароль с хешем, сохранённым в БД
    static bool verify_password(const std::string& password, const std::string& stored)
    {
        auto sep = stored.find(':');
        if (sep == std::string::npos)
            return false;

        std::string salt_hex = stored.substr(0, sep);
        std::string hash_hex = stored.substr(sep + 1);

        auto salt = from_hex(salt_hex);

        unsigned char computed_hash[32];
        PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            salt.data(), static_cast<int>(salt.size()),
            100000,
            EVP_sha256(),
            sizeof(computed_hash), computed_hash
        );

        std::string computed_hex = to_hex(computed_hash, sizeof(computed_hash));

        return constant_time_equal(computed_hex, hash_hex);
    }

private:
    static std::string to_hex(const unsigned char* data, size_t len)
    {
        std::ostringstream oss;
        for (size_t i = 0; i < len; i++)
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
        return oss.str();
    }

    static std::vector<unsigned char> from_hex(const std::string& hex)
    {
        std::vector<unsigned char> result;
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            unsigned char byte = static_cast<unsigned char>(
                std::stoi(hex.substr(i, 2), nullptr, 16)
                );
            result.push_back(byte);
        }
        return result;
    }

    // Сравнение без "утечки" времени выполнения (защита от timing-атак)
    static bool constant_time_equal(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;

        unsigned char result = 0;
        for (size_t i = 0; i < a.size(); i++)
        {
            result |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
        }

        return result == 0;
    }
};