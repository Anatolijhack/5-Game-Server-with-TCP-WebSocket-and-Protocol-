//#pragma once
//#include <string>
//#include <fstream>
//#include <mutex>
//#include <sstream>
//#include <chrono>
//#include <iomanip>
//#include <iostream>
//
//enum class LogLevel
//{
//    Info,
//    Warning,
//    Error
//};
//
//class Logger
//{
//public:
//    static Logger& instance()
//    {
//        static Logger logger;
//        return logger;
//    }
//
//    void init(const std::string& file_path)
//    {
//        std::lock_guard<std::mutex> lock(mtx);
//        log_file.open(file_path, std::ios::app);
//    }
//
//    void log(LogLevel level, const std::string& message)
//    {
//        std::lock_guard<std::mutex> lock(mtx);
//
//        std::string line = format_line(level, message);
//
//        std::cout << line << std::endl;
//
//        if (log_file.is_open())
//        {
//            log_file << line << std::endl;
//            log_file.flush();
//        }
//    }
//
//    void info(const std::string& message) { log(LogLevel::Info, message); }
//    void warning(const std::string& message) { log(LogLevel::Warning, message); }
//    void error(const std::string& message) { log(LogLevel::Error, message); }
//
//private:
//    std::mutex mtx;
//    std::ofstream log_file;
//
//    Logger() = default;
//
//    std::string level_to_string(LogLevel level)
//    {
//        switch (level)
//        {
//        case LogLevel::Info:    return "INFO";
//        case LogLevel::Warning: return "WARN";
//        case LogLevel::Error:   return "ERROR";
//        }
//        return "UNKNOWN";
//    }
//
//    std::string format_line(LogLevel level, const std::string& message)
//    {
//        auto now = std::chrono::system_clock::now();
//        auto time = std::chrono::system_clock::to_time_t(now);
//
//        std::tm tm_buf;
//        localtime_s(&tm_buf, &time); // Windows-специфично; дл€ Linux Ч localtime_r
//
//        std::ostringstream oss;
//        oss << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << "] "
//            << "[" << level_to_string(level) << "] "
//            << message;
//
//        return oss.str();
//    }
//};
//
//// ”добные макросы
//#define LOG_INFO(msg)    Logger::instance().info(msg)
//#define LOG_WARNING(msg) Logger::instance().warning(msg)
//#define LOG_ERROR(msg)   Logger::instance().error(msg)


#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <filesystem>

enum class LogLevel
{
    Info,
    Warning,
    Error
};

class Logger
{
public:
    static Logger& instance()
    {
        static Logger logger;
        return logger;
    }

    void init(const std::string& file_path, size_t max_size_bytes = 10 * 1024 * 1024, int max_backups = 5)
    {
        std::lock_guard<std::mutex> lock(mtx);

        this->file_path = file_path;
        this->max_size_bytes = max_size_bytes;
        this->max_backups = max_backups;

        log_file.open(file_path, std::ios::app);
    }

    void log(LogLevel level, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mtx);

        rotate_if_needed_locked();

        std::string line = format_line(level, message);

        std::cout << line << std::endl;

        if (log_file.is_open())
        {
            log_file << line << std::endl;
            log_file.flush();
        }
    }

    void info(const std::string& message) { log(LogLevel::Info, message); }
    void warning(const std::string& message) { log(LogLevel::Warning, message); }
    void error(const std::string& message) { log(LogLevel::Error, message); }

private:
    std::mutex mtx;
    std::ofstream log_file;
    std::string file_path;
    size_t max_size_bytes = 10 * 1024 * 1024;
    int max_backups = 5;

    Logger() = default;

    std::string level_to_string(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        }
        return "UNKNOWN";
    }

    std::string format_line(LogLevel level, const std::string& message)
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);

        std::tm tm_buf;
        localtime_s(&tm_buf, &time);

        std::ostringstream oss;
        oss << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << "] "
            << "[" << level_to_string(level) << "] "
            << message;

        return oss.str();
    }

    // ¬ызываетс€ уже под захваченным mtx Ч не берЄт лок повторно
    void rotate_if_needed_locked()
    {
        if (!log_file.is_open() || file_path.empty())
            return;

        std::error_code ec;
        auto size = std::filesystem::file_size(file_path, ec);

        if (ec || size < max_size_bytes)
            return;

        log_file.close();

        std::string backup_name = make_backup_name();

        std::filesystem::rename(file_path, backup_name, ec);
        if (ec)
        {
            // ≈сли переименовать не удалось (например, файл зан€т), просто продолжаем писать в тот же файл
            log_file.open(file_path, std::ios::app);
            return;
        }

        log_file.open(file_path, std::ios::app);

        cleanup_old_backups_locked();
    }

    std::string make_backup_name()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);

        std::tm tm_buf;
        localtime_s(&tm_buf, &time);

        std::ostringstream oss;
        oss << file_path << "." << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");

        return oss.str();
    }

    void cleanup_old_backups_locked()
    {
        namespace fs = std::filesystem;

        fs::path p(file_path);
        std::string dir = p.has_parent_path() ? p.parent_path().string() : ".";
        std::string prefix = p.filename().string() + ".";

        std::vector<fs::path> backups;

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec))
        {
            if (ec) break;

            std::string name = entry.path().filename().string();

            if (name.rfind(prefix, 0) == 0) // начинаетс€ с "server.log."
            {
                backups.push_back(entry.path());
            }
        }

        if (static_cast<int>(backups.size()) <= max_backups)
            return;

        // »мена содержат метку времени в формате YYYYMMDD_HHMMSS Ч сортировка по имени = сортировка по времени
        std::sort(backups.begin(), backups.end());

        size_t to_delete = backups.size() - max_backups;

        for (size_t i = 0; i < to_delete; i++)
        {
            fs::remove(backups[i], ec);
        }
    }
};

#define LOG_INFO(msg)    Logger::instance().info(msg)
#define LOG_WARNING(msg) Logger::instance().warning(msg)
#define LOG_ERROR(msg)   Logger::instance().error(msg)