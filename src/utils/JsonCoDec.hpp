// src/utils/JsonCoDec.hpp
// JSON 编解码工具 — 纯静态方法，零状态。
//
//   Encode（序列化）：build消息对象 → 拼HTTP body → JSON转义
//   Decode（反序列化）：从OpenAI兼容响应提取content → JSON反转义
//
// 所有方法为 static，禁止实例化。

#pragma once

#include <sstream>
#include <string>
#include <string_view>

namespace utils
{

class JsonCoDec
{
  public:
    JsonCoDec() = delete;

    // ===== Encode =====

    // JSON 字符转义（不写入流，返回新字符串）
    static std::string encode(std::string_view s);

    // JSON 字符转义（追加到已有流，避免额外拷贝）
    static void encodeTo(std::ostringstream &oss, std::string_view s);

    // 构造单个消息对象：{"role":"user","content":"你好"}
    static std::string buildMsgObj(std::string_view role, std::string_view content);

    // 构造 OpenAI 兼容 HTTP 请求体
    // messagesJson 已经是完整的 JSON 数组：[{"role":"user",...}, ...]
    static std::string buildHttpBody(std::string_view model, std::string_view messagesJson,
                                     double temperature);

    // ===== Decode =====

    // 从 OpenAI 兼容 API 响应中提取 assistant 回复内容
    static std::string extractContent(std::string_view responseBody);

    // JSON 字符反转义（处理 \" \\ \n 等）
    static std::string decode(std::string_view s);
};

} // namespace utils
