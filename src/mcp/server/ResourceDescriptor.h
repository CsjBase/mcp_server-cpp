#pragma once

#include <optional>
#include <functional>
#include <nlohmann/json.hpp>

namespace mcp
{

    using json = nlohmann::json;

    struct ResourceDescriptor
    {
        std::string uri;  // 唯一 URI，如 "file:///data/config.json"
        std::string name; // 人类可读名称
        std::optional<std::string> description;
        std::optional<std::string> mime_type;

        // 读取时返回 {"contents":[{"uri":...,"mimeType":...,"text":...}]}
        std::function<json()> reader;

        json to_json() const
        {
            json j = {{"uri", uri}, {"name", name}};
            if (description)
                j["description"] = *description;
            if (mime_type)
                j["mimeType"] = *mime_type;
            return j;
        }
    };

    using TemplateReader = std::function<json(
        const std::string &uri,
        const std::unordered_map<std::string, std::string> &params)>;

    // 资源模板：参数化 URI，使用 RFC 6570 模板语法
    struct ResourceTemplateDescriptor
    {
        std::string uri_template; // 如 "file:///logs/{date}.log"
        std::string name;
        std::optional<std::string> description;
        std::optional<std::string> mime_type;
        TemplateReader reader; // 新增

        json to_json() const
        {
            json j = {{"uriTemplate", uri_template}, {"name", name}};
            if (description)
                j["description"] = *description;
            if (mime_type)
                j["mimeType"] = *mime_type;
            return j;
        }
    };

} // namespace mcp