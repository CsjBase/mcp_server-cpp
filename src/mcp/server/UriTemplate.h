#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

namespace mcp
{

    // 编译后的 URI 模板。构造时解析一次，之后 match() 可反复调用。
    class UriTemplate
    {
    public:
        explicit UriTemplate(std::string tmpl);

        // 匹配成功返回提取到的变量表；失败返回 nullopt。
        // 匹配要求 URI 被模板完全覆盖（不允许前缀匹配）。
        std::optional<std::unordered_map<std::string, std::string>>
        match(const std::string &uri) const;

        const std::string &original() const { return original_; }
        bool has_variables() const { return has_vars_; }

    private:
        struct Segment
        {
            enum class Type
            {
                Literal,
                Var,
                ReservedVar
            };
            Type type;
            std::string value; // Literal: 字面内容；Var/ReservedVar: 变量名
        };

        void parse(const std::string &tmpl);

        std::string original_;
        std::vector<Segment> segments_;
        bool has_vars_ = false;
    };

} // namespace mcp