#include "utils/JsonCoDec.hpp"
#include "utils/TryCatch.hpp"

#include <nlohmann/json.hpp>

namespace utils
{

std::string JsonCoDec::buildMsgObj(std::string_view role, std::string_view content)
{
    nlohmann::json obj = {{"role", std::string(role)}, {"content", std::string(content)}};
    return obj.dump();
}

std::string JsonCoDec::buildHttpBody(std::string_view model, std::string_view messagesJson,
                                     double temperature)
{
    return utils::tryOrFailed(
        [&]() -> std::string
        {
            nlohmann::json body = {{"model", std::string(model)},
                                   {"messages", nlohmann::json::parse(messagesJson)},
                                   {"temperature", temperature}};
            return body.dump();
        },
        []() { return std::string{}; });
}

std::string JsonCoDec::extractContent(std::string_view responseBody)
{
    return utils::tryOrFailed(
        [&]() -> std::string
        {
            auto root = nlohmann::json::parse(responseBody);
            const auto &choices = root.at("choices");
            if (choices.empty())
            {
                return {};
            }
            return choices.back()
                .value("message", nlohmann::json::object())
                .value("content", std::string{});
        },
        []() { return std::string{}; });
}

} // namespace utils
