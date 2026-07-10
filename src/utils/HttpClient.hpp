// src/utils/HttpClient.hpp
// 同步 HTTP 客户端 — 基于 libcurl 的轻量封装。
//
// 所有方法为 static，零状态。仅负责网络通信，不涉及业务 JSON 组装/拆解。

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace utils
{

class HttpClient
{
  public:
    HttpClient() = delete;

    struct Response
    {
        int64_t httpCode = 0; // HTTP 状态码；<0 = curl 层错误
        std::string body;     // 成功时=响应体，失败时=已格式化的错误信息

        bool isSuccess() const
        {
            return httpCode >= 200 and httpCode < 300;
        }
    };

    // 发送同步 HTTP POST（JSON body + Bearer Token 认证）
    //
    // url:        完整请求地址
    // jsonBody:   POST body（已序列化的 JSON 字符串）
    // authToken:  Bearer token（不含 "Bearer " 前缀）
    // timeoutSec: 超时秒数，默认 60
    //
    // 返回 Response{httpCode, body}。调用者根据 httpCode 判断成功/失败。
    // 此方法阻塞当前线程直到请求完成或超时。
    static Response postJson(std::string_view url, std::string_view jsonBody,
                             std::string_view authToken, int64_t timeoutSec = 60);
};

} // namespace utils
