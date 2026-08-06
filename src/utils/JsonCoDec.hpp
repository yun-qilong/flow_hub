// src/utils/JsonCoDec.hpp
// JSON 编解码工具 — 纯静态方法，零状态。
//
//   Encode（序列化）：build消息对象 → 拼HTTP body → JSON转义
//   Decode（反序列化）：从OpenAI兼容响应提取content → JSON反转义
//
// 所有方法为 static，禁止实例化。

#pragma once

#include <string>
#include <string_view>

namespace utils
{

class JsonCoDec
{
  public:
    JsonCoDec() = delete;

    static std::string buildMsgObj(std::string_view role, std::string_view content);
    static std::string buildHttpBody(std::string_view model, std::string_view messagesJson,
                                     double temperature);
    static std::string extractContent(std::string_view responseBody);
};

} // namespace utils
