//#pragma once
//#include <functional>
//#include <string>
//#include <iostream>
//#include <unordered_map>
//#include "Structs.h"
//#include <sstream>
//#include <vector>
//
//
//
//using Handler = std::function<Response(const Request&)>;
//
//class Router {
//private:
//    struct Route {
//        std::string method;
//        std::string path;
//        std::function<Response(const Request&)> handler;
//    };
//
//    std::vector<Route> routes;
//
//    //bool match(const std::string& route_path,
//    //    const std::string& request_path,
//    //    Request& req)
//    //{
//    //    std::istringstream r(route_path);
//    //    std::istringstream p(request_path);
//
//    //    std::string r_seg, p_seg;
//
//    //    while (true)
//    //    {
//    //        bool route_ok = static_cast<bool>(
//    //            std::getline(r, r_seg, '/')
//    //            );
//
//    //        bool request_ok = static_cast<bool>(
//    //            std::getline(p, p_seg, '/')
//    //            );
//
//    //        // Îáà ïóòè çàêîí÷èëèñü îäíîâðåìåííî
//    //        if (!route_ok && !request_ok)
//    //            return true;
//
//    //        // Îäèí çàêîí÷èëñÿ ðàíüøå äðóãîãî
//    //        if (!route_ok || !request_ok)
//    //            return false;
//
//    //        // Ïàðàìåòð :id
//    //        if (!r_seg.empty() && r_seg[0] == ':')
//    //        {
//    //            std::string param_name = r_seg.substr(1);
//
//    //            if (param_name.empty())
//    //                return false;
//
//    //            req.params[param_name] = p_seg;
//    //        }
//    //        else
//    //        {
//    //            if (r_seg != p_seg)
//    //                return false;
//    //        }
//    //    }
//    //}
//    bool match(const std::string& route_path,
//        const std::string& request_path,
//        Request& req)
//    {
//        // Для маршрутизации используем только путь,
//        // query string (?search=...&limit=...) не учитываем.
//        std::string clean_request_path = request_path;
//
//        size_t query_pos = clean_request_path.find('?');
//
//        if (query_pos != std::string::npos)
//        {
//            clean_request_path =
//                clean_request_path.substr(0, query_pos);
//        }
//
//        std::istringstream r(route_path);
//        std::istringstream p(clean_request_path);
//
//        std::string r_seg, p_seg;
//
//        while (true)
//        {
//            bool route_ok = static_cast<bool>(
//                std::getline(r, r_seg, '/')
//                );
//
//            bool request_ok = static_cast<bool>(
//                std::getline(p, p_seg, '/')
//                );
//
//            // Оба пути закончились одновременно
//            if (!route_ok && !request_ok)
//                return true;
//
//            // Один закончился раньше другого
//            if (!route_ok || !request_ok)
//                return false;
//
//            // Параметр :id
//            if (!r_seg.empty() && r_seg[0] == ':')
//            {
//                std::string param_name = r_seg.substr(1);
//
//                if (param_name.empty())
//                    return false;
//
//                req.params[param_name] = p_seg;
//            }
//            else
//            {
//                if (r_seg != p_seg)
//                    return false;
//            }
//        }
//    }
//
//public:
//    void add(const std::string& method,
//        const std::string& path,
//        std::function<Response(const Request&)> handler)
//    {
//        routes.push_back({ method, path, handler });
//    }
//
//    //Response route(Request req)
//    //{
//    //    std::cout << "ROUTER: " << req.method
//    //        << " " << req.path << std::endl;
//
//    //    for (const auto& r : routes)
//    //    {
//    //        req.params.clear();
//
//    //        std::cout << "CHECK: "
//    //            << r.method << " "
//    //            << r.path << std::endl;
//
//    //        if (r.method != req.method)
//    //            continue;
//
//    //        if (match(r.path, req.path, req))
//    //        {
//    //            std::cout << "MATCH!" << std::endl;
//
//    //            try
//    //            {
//    //                Response response = r.handler(req);
//
//    //                std::cout << "HANDLER DONE" << std::endl;
//
//    //                return response;
//    //            }
//    //            catch (const std::exception& e)
//    //            {
//    //                std::cerr << "HANDLER EXCEPTION: "
//    //                    << e.what()
//    //                    << std::endl;
//
//    //                return Response{
//    //                    "Internal Server Error",
//    //                    "text/plain",
//    //                    "500 Internal Server Error"
//    //                };
//    //            }
//    //        }
//    //    }
//
//    //    std::cout << "NOT FOUND" << std::endl;
//
//    //    return Response{
//    //        "Not Found",
//    //        "text/plain",
//    //        "404 Not Found"
//    //    };
//    //}
//    Response route(Request req)
//    {
//        std::cout << "ROUTER: " << req.method
//            << " " << req.path << std::endl;
//
//
//        if (req.method == "OPTIONS")
//        {
//            return Response{ "", "text/plain", "204 No Content" };
//        }
//
//        // Разбираем query-параметры из URL
//        size_t query_pos = req.path.find('?');
//
//        if (query_pos != std::string::npos)
//        {
//            std::string query = req.path.substr(query_pos + 1);
//
//            std::stringstream ss(query);
//            std::string pair;
//
//            while (std::getline(ss, pair, '&'))
//            {
//                size_t equal_pos = pair.find('=');
//
//                if (equal_pos == std::string::npos)
//                    continue;
//
//                std::string key = pair.substr(0, equal_pos);
//                std::string value = pair.substr(equal_pos + 1);
//
//                req.params[key] = value;
//            }
//        }
//
//        for (const auto& r : routes)
//        {
//            // НЕ ДЕЛАЕМ req.params.clear() здесь,
//            // иначе удалим query-параметры.
//
//            std::cout << "CHECK: "
//                << r.method << " "
//                << r.path << std::endl;
//
//            if (r.method != req.method)
//                continue;
//
//            if (match(r.path, req.path, req))
//            {
//                std::cout << "MATCH!" << std::endl;
//
//                try
//                {
//                    Response response = r.handler(req);
//
//                    std::cout << "HANDLER DONE" << std::endl;
//
//                    return response;
//                }
//                catch (const std::exception& e)
//                {
//                    std::cerr << "HANDLER EXCEPTION: "
//                        << e.what()
//                        << std::endl;
//
//                    return Response{
//                        "Internal Server Error",
//                        "text/plain",
//                        "500 Internal Server Error"
//                    };
//                }
//            }
//        }
//
//        std::cout << "NOT FOUND" << std::endl;
//
//        return Response{
//            "Not Found",
//            "text/plain",
//            "404 Not Found"
//        };
//    }
//};

#pragma once
#include <functional>
#include <string>
#include <iostream>
#include <unordered_map>
#include "Structs.h"
#include <sstream>
#include <vector>

using Handler = std::function<Response(const Request&)>;

class Router {
private:
    struct Route {
        std::string method;
        std::string path;
        std::function<Response(const Request&)> handler;
    };

    std::vector<Route> routes;

    static int hex_val(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // %D0%9D -> байты UTF-8. В query '+' означает пробел, в пути остаётся плюсом.
    static std::string url_decode(const std::string& s, bool plus_as_space)
    {
        std::string out;
        out.reserve(s.size());

        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '%' && i + 2 < s.size() &&
                hex_val(s[i + 1]) >= 0 && hex_val(s[i + 2]) >= 0)
            {
                char c = (char)(hex_val(s[i + 1]) * 16 + hex_val(s[i + 2]));
                if (c != '\0') out += c;      // нулевой байт отбрасываем
                i += 2;
            }
            else if (s[i] == '+' && plus_as_space)
                out += ' ';
            else
                out += s[i];
        }
        return out;
    }

    bool match(const std::string& route_path,
        const std::string& request_path,
        Request& req)
    {
        // Для маршрутизации используем только путь, query string не учитываем
        std::string clean_request_path = request_path;

        size_t query_pos = clean_request_path.find('?');
        if (query_pos != std::string::npos)
            clean_request_path = clean_request_path.substr(0, query_pos);

        // /chat/groups/ и /chat/groups считаем одним путём
        if (clean_request_path.size() > 1 && clean_request_path.back() == '/')
            clean_request_path.pop_back();

        std::istringstream r(route_path);
        std::istringstream p(clean_request_path);

        std::string r_seg, p_seg;

        // Параметры пути копим отдельно и записываем в req только при полном
        // совпадении, иначе неудачные маршруты оставляют мусор в req.params
        std::unordered_map<std::string, std::string> found;

        while (true)
        {
            bool route_ok = static_cast<bool>(std::getline(r, r_seg, '/'));
            bool request_ok = static_cast<bool>(std::getline(p, p_seg, '/'));

            // Оба пути закончились одновременно: совпало
            if (!route_ok && !request_ok)
            {
                for (auto& kv : found)
                    req.params[kv.first] = kv.second;   // путь важнее query
                return true;
            }

            // Один закончился раньше другого
            if (!route_ok || !request_ok)
                return false;

            // Параметр :name, :id и т.п.
            if (!r_seg.empty() && r_seg[0] == ':')
            {
                std::string param_name = r_seg.substr(1);

                if (param_name.empty())
                    return false;

                found[param_name] = url_decode(p_seg, false);
            }
            else
            {
                if (r_seg != p_seg)
                    return false;
            }
        }
    }

public:
    void add(const std::string& method,
        const std::string& path,
        std::function<Response(const Request&)> handler)
    {
        routes.push_back({ method, path, handler });
    }

    Response route(Request req)
    {
        std::cout << "ROUTER: " << req.method
            << " " << req.path << std::endl;

        if (req.method == "OPTIONS")
        {
            return Response{ "", "text/plain", "204 No Content" };
        }

        // Разбираем query-параметры из URL
        size_t query_pos = req.path.find('?');

        if (query_pos != std::string::npos)
        {
            std::string query = req.path.substr(query_pos + 1);

            std::stringstream ss(query);
            std::string pair;

            while (std::getline(ss, pair, '&'))
            {
                size_t equal_pos = pair.find('=');

                if (equal_pos == std::string::npos)
                    continue;

                std::string key = pair.substr(0, equal_pos);
                std::string value = pair.substr(equal_pos + 1);

                req.params[url_decode(key, true)] = url_decode(value, true);
            }
        }

        for (const auto& r : routes)
        {
            if (r.method != req.method)
                continue;

            if (match(r.path, req.path, req))
            {
                std::cout << "MATCH: " << r.method << " " << r.path << std::endl;

                try
                {
                    Response response = r.handler(req);

                    std::cout << "HANDLER DONE" << std::endl;

                    return response;
                }
                catch (const std::exception& e)
                {
                    std::cerr << "HANDLER EXCEPTION: "
                        << e.what()
                        << std::endl;

                    return Response{
                        "Internal Server Error",
                        "text/plain",
                        "500 Internal Server Error"
                    };
                }
            }
        }

        std::cout << "NOT FOUND" << std::endl;

        return Response{
            "Not Found",
            "text/plain",
            "404 Not Found"
        };
    }
};