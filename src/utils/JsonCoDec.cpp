// src/utils/JsonCoDec.cpp

#include "utils/JsonCoDec.hpp"

#include <cstring>

namespace utils
{

// ===== encode =====
std::string JsonCoDec::encode(std::string_view s)
{
    std::ostringstream oss;
    encodeTo(oss, s);
    return oss.str();
}

// ===== encodeTo =====
void JsonCoDec::encodeTo(std::ostringstream &oss, std::string_view s)
{
    for (char ch : s)
    {
        if (ch == '"')
            oss << R"(\")";
        else if (ch == '\\')
            oss << R"(\\)";
        else if (ch == '\n')
            oss << R"(\n)";
        else if (ch == '\r')
            oss << R"(\r)";
        else if (ch == '\t')
            oss << R"(\t)";
        else
            oss << ch;
    }
}

// ===== decode =====
std::string JsonCoDec::decode(std::string_view s)
{
    std::string result;
    result.reserve(s.size());

    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' and i + 1 < s.size())
        {
            char escaped = s[i + 1];
            if (escaped == '"')
                result += '"';
            else if (escaped == 'n')
                result += '\n';
            else if (escaped == 'r')
                result += '\r';
            else if (escaped == 't')
                result += '\t';
            else if (escaped == '\\')
                result += '\\';
            else
            {
                result += '\\';
                result += escaped;
            }
            ++i; // skip escaped char
        }
        else
        {
            result += s[i];
        }
    }
    return result;
}

// ===== buildMsgObj =====
std::string JsonCoDec::buildMsgObj(std::string_view role, std::string_view content)
{
    std::ostringstream oss;
    oss << R"({"role":")" << role << R"(","content":")";
    encodeTo(oss, content);
    oss << R"("})";
    return oss.str();
}

// ===== buildHttpBody =====
std::string JsonCoDec::buildHttpBody(std::string_view model, std::string_view messagesJson,
                                     double temperature)
{
    std::ostringstream oss;
    oss << R"({"model":")" << model << R"(","messages":)" << messagesJson << R"(,"temperature":)"
        << temperature << "}";
    return oss.str();
}

// ===== extractContent =====
std::string JsonCoDec::extractContent(std::string_view responseBody)
{
    const char key[] = R"("content":")";
    const char *data = responseBody.data();
    size_t size = responseBody.size();

    // 找最后一个 "content":" 出现位置（跳过 finish_reason 等前面的）
    const char *last = nullptr;
    const char *pos = data;
    for (size_t i = 0; i + sizeof(key) - 1 <= size; ++i)
    {
        if (std::strncmp(pos + i, key, sizeof(key) - 1) == 0)
        {
            last = pos + i;
            i += sizeof(key) - 2; // 跳过当前匹配，继续搜后面的
        }
    }

    if (last == nullptr)
    {
        return "[parse error: no content field]";
    }

    last += sizeof(key) - 1; // 跳过 "content":"

    // 读取到下一个未转义的 "
    std::string raw;
    const char *end = pos + size;
    while (last < end and *last != '"')
    {
        if (*last == '\\' and last + 1 < end)
        {
            raw += *last;
            ++last;
            raw += *last;
            ++last;
        }
        else
        {
            raw += *last;
            ++last;
        }
    }

    return raw.empty() ? "[empty response]" : decode(raw);
}

} // namespace utils
