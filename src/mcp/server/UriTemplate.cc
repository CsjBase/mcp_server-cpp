#include "UriTemplate.h"

#include <stdexcept>

namespace mcp
{

    UriTemplate::UriTemplate(std::string tmpl) : original_(std::move(tmpl))
    {
        parse(original_);
    }

    void UriTemplate::parse(const std::string &tmpl)
    {
        std::string literal;
        size_t i = 0;

        while (i < tmpl.size())
        {
            if (tmpl[i] != '{')
            {
                literal.push_back(tmpl[i++]);
                continue;
            }

            // 找到闭合的 '}'。RFC 6570 不允许嵌套，直接找下一个即可。
            size_t end = tmpl.find('}', i + 1);
            if (end == std::string::npos)
            {
                throw std::invalid_argument(
                    "Unclosed '{' in URI template: " + tmpl);
            }

            std::string expr = tmpl.substr(i + 1, end - i - 1);
            if (expr.empty())
            {
                throw std::invalid_argument(
                    "Empty expression in URI template: " + tmpl);
            }

            // 解析前缀。MCP 资源模板通常只用 {var} 和 {+var}。
            Segment::Type type = Segment::Type::Var;
            std::string var_name;
            switch (expr[0])
            {
            case '+':
                type = Segment::Type::ReservedVar;
                var_name = expr.substr(1);
                break;
            case '?':
            case '#':
            case '/':
            case '.':
            case ';':
            case '&':
                // 查询/片段/路径扩展暂不支持。明确抛错，
                // 避免静默降级导致运行时匹配行为与模板声明不符。
                throw std::invalid_argument(
                    "Unsupported URI template operator '" +
                    std::string(1, expr[0]) + "' in: " + tmpl);
            default:
                var_name = expr;
                break;
            }

            if (var_name.empty())
            {
                throw std::invalid_argument(
                    "Empty variable name in URI template: " + tmpl);
            }
            if (var_name.find(',') != std::string::npos)
            {
                throw std::invalid_argument(
                    "Multi-variable expansion not supported: " + tmpl);
            }

            if (!literal.empty())
            {
                segments_.push_back({Segment::Type::Literal, std::move(literal)});
                literal.clear();
            }
            segments_.push_back({type, std::move(var_name)});
            has_vars_ = true;
            i = end + 1;
        }

        if (!literal.empty())
        {
            segments_.push_back({Segment::Type::Literal, std::move(literal)});
        }
    }

    std::optional<std::unordered_map<std::string, std::string>>
    UriTemplate::match(const std::string &uri) const
    {
        // 无变量模板：精确匹配
        if (!has_vars_)
        {
            if (uri == original_)
            {
                return std::unordered_map<std::string, std::string>{};
            }
            return std::nullopt;
        }

        std::unordered_map<std::string, std::string> params;
        size_t pos = 0;

        for (size_t i = 0; i < segments_.size(); ++i)
        {
            const auto &seg = segments_[i];

            if (seg.type == Segment::Type::Literal)
            {
                // 字面量必须精确出现在当前位置
                if (uri.compare(pos, seg.value.size(), seg.value) != 0)
                {
                    return std::nullopt;
                }
                pos += seg.value.size();
                continue;
            }

            // 变量：寻找下一个字面量作为结束边界。
            // 非贪婪策略——取最早出现的字面量，让变量尽可能短。
            std::string next_literal;
            bool has_next_literal = false;
            if (i + 1 < segments_.size() &&
                segments_[i + 1].type == Segment::Type::Literal)
            {
                next_literal = segments_[i + 1].value;
                has_next_literal = true;
            }

            size_t value_end;
            if (has_next_literal)
            {
                value_end = uri.find(next_literal, pos);
                if (value_end == std::string::npos)
                {
                    return std::nullopt;
                }
            }
            else
            {
                value_end = uri.size();
            }

            std::string value = uri.substr(pos, value_end - pos);

            // 空值对资源 URI 无意义，拒绝。
            if (value.empty())
            {
                return std::nullopt;
            }
            // 普通变量不得跨越路径分隔符；{+var} 例外。
            if (seg.type == Segment::Type::Var &&
                value.find('/') != std::string::npos)
            {
                return std::nullopt;
            }

            params[seg.value] = std::move(value);
            pos = value_end;
        }

        // URI 必须被完全消耗，不接受尾部剩余内容。
        if (pos != uri.size())
        {
            return std::nullopt;
        }
        return params;
    }

} // namespace mcp