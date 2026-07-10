// src/utils/HttpClient.cpp

#include "utils/HttpClient.hpp"

#include <curl/curl.h>

#include <iostream>

namespace utils
{

// ===== curl 写回调（将响应数据追加到 string） =====
static size_t writeCallback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *body = static_cast<std::string *>(userdata);
    size_t total = size * nmemb;
    body->append(static_cast<const char *>(ptr), total);
    return total;
}

// ===== postJson =====
HttpClient::Response HttpClient::postJson(std::string_view url, std::string_view jsonBody,
                                          std::string_view authToken, int64_t timeoutSec)
{
    Response resp;

    CURL *curl = curl_easy_init();
    if (curl == nullptr)
    {
        resp.httpCode = -1;
        resp.body = "curl_easy_init failed";
        return resp;
    }

    // 构造 Authorization header
    std::string authHeader = "Authorization: Bearer ";
    authHeader += authToken;

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, authHeader.c_str());

    std::string urlStr(url);
    std::string bodyStr(jsonBody);

    curl_easy_setopt(curl, CURLOPT_URL, urlStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<int64_t>(bodyStr.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec);

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK)
    {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.httpCode);
        // HTTP 错误时，把状态码也塞进 body，外面不用再拼
        if (resp.httpCode < 200 or resp.httpCode >= 300)
        {
            std::string prefix = "HTTP " + std::to_string(resp.httpCode) + ": ";
            resp.body.insert(0, prefix);
        }
    }
    else
    {
        resp.httpCode = -static_cast<int64_t>(res);
        resp.body = std::string("curl error: ") + curl_easy_strerror(res);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return resp;
}

} // namespace utils
