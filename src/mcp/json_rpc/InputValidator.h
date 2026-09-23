#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <regex>

namespace mcp
{

    using json = nlohmann::json;

    // 结构层校验器：在注册时从 inputSchema 编译生成，避免每请求重新解析
    class InputValidator
    {
    public:
        explicit InputValidator(json schema) : schema_(std::move(schema))
        {
            compile(schema_);
        }

        // 校验 args 是否符合 inputSchema
        bool validate(const json &args)
        {
            last_errors_.clear();
            validate_node(schema_, args, "", last_errors_);
            return last_errors_.empty();
        }

        const json &last_errors() const { return last_errors_; }

        // 获取原始 schema，供 tools/list 返回
        const json &schema() const { return schema_; }

    private:
        json schema_;
        json last_errors_ = json::array();

        // 预编译：提取常用约束，避免每请求遍历完整 schema
        struct CompiledConstraints
        {
            std::string type;
            std::vector<std::string> required;
            std::vector<json> enum_values;
            std::optional<double> minimum;
            std::optional<double> maximum;
            std::optional<int64_t> min_length;
            std::optional<int64_t> max_length;
            std::string pattern;
        };
        std::unordered_map<std::string, CompiledConstraints> compiled_;

        void compile(const json &schema)
        {
            if (!schema.is_object())
                return;
            CompiledConstraints cc;
            if (schema.contains("type"))
                cc.type = schema["type"].get<std::string>();
            if (schema.contains("required") && schema["required"].is_array())
            {
                for (const auto &r : schema["required"])
                    cc.required.push_back(r.get<std::string>());
            }
            if (schema.contains("enum") && schema["enum"].is_array())
                cc.enum_values = schema["enum"];
            if (schema.contains("minimum"))
                cc.minimum = schema["minimum"].get<double>();
            if (schema.contains("maximum"))
                cc.maximum = schema["maximum"].get<double>();
            if (schema.contains("minLength"))
                cc.min_length = schema["minLength"].get<int64_t>();
            if (schema.contains("maxLength"))
                cc.max_length = schema["maxLength"].get<int64_t>();
            if (schema.contains("pattern"))
                cc.pattern = schema["pattern"].get<std::string>();
            compiled_[""] = std::move(cc);

            // 递归编译子 schema（properties 等）
            if (schema.contains("properties") && schema["properties"].is_object())
            {
                for (auto &[key, sub] : schema["properties"].items())
                {
                    compile(sub);
                }
            }
        }

        void validate_node(const json &schema, const json &value,
                           const std::string &path, json &errors)
        {
            // 空 schema 表示任意值
            if (schema.empty())
                return;

            // type 校验
            if (schema.contains("type"))
            {
                const auto &type = schema["type"];
                bool type_ok = false;
                if (type.is_string())
                {
                    type_ok = check_type(type.get<std::string>(), value);
                }
                else if (type.is_array())
                {
                    for (const auto &t : type)
                    {
                        if (check_type(t.get<std::string>(), value))
                        {
                            type_ok = true;
                            break;
                        }
                    }
                }
                if (!type_ok)
                {
                    errors.push_back({{"path", path},
                                      {"message", "expected type " + type.dump() +
                                                      ", got " + std::string(value.type_name())}});
                    return; // 类型错误后不再继续校验
                }
            }

            // required 校验（仅对 object）
            if (schema.contains("required") && value.is_object())
            {
                for (const auto &req : schema["required"])
                {
                    std::string key = req.get<std::string>();
                    if (!value.contains(key))
                    {
                        errors.push_back({{"path", path.empty() ? key : path + "." + key},
                                          {"message", "required field missing"}});
                    }
                }
            }

            // enum 校验
            if (schema.contains("enum") && !value.is_null())
            {
                bool found = false;
                for (const auto &e : schema["enum"])
                {
                    if (value == e)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    errors.push_back({{"path", path},
                                      {"message", "value not in enum"}});
                }
            }

            // 数值范围
            if (value.is_number())
            {
                if (schema.contains("minimum") &&
                    value.get<double>() < schema["minimum"].get<double>())
                {
                    errors.push_back({{"path", path},
                                      {"message", "value below minimum " +
                                                      schema["minimum"].dump()}});
                }
                if (schema.contains("maximum") &&
                    value.get<double>() > schema["maximum"].get<double>())
                {
                    errors.push_back({{"path", path},
                                      {"message", "value above maximum " +
                                                      schema["maximum"].dump()}});
                }
            }

            // 字符串长度和正则
            if (value.is_string())
            {
                auto len = static_cast<int64_t>(value.get<std::string>().size());
                if (schema.contains("minLength") &&
                    len < schema["minLength"].get<int64_t>())
                {
                    errors.push_back({{"path", path},
                                      {"message", "string too short, min " +
                                                      schema["minLength"].dump()}});
                }
                if (schema.contains("maxLength") &&
                    len > schema["maxLength"].get<int64_t>())
                {
                    errors.push_back({{"path", path},
                                      {"message", "string too long, max " +
                                                      schema["maxLength"].dump()}});
                }
                if (schema.contains("pattern"))
                {
                    try
                    {
                        std::regex re(schema["pattern"].get<std::string>());
                        if (!std::regex_match(value.get<std::string>(), re))
                        {
                            errors.push_back({{"path", path},
                                              {"message", "string does not match pattern"}});
                        }
                    }
                    catch (...)
                    { /* 无效正则，跳过 */
                    }
                }
            }

            // object 的 properties 递归
            if (value.is_object() && schema.contains("properties"))
            {
                for (auto &[key, sub_schema] : schema["properties"].items())
                {
                    if (value.contains(key))
                    {
                        validate_node(sub_schema, value[key],
                                      path.empty() ? key : path + "." + key,
                                      errors);
                    }
                }
            }

            // array 的 items 递归
            if (value.is_array() && schema.contains("items"))
            {
                for (size_t i = 0; i < value.size(); ++i)
                {
                    validate_node(schema["items"], value[i],
                                  path + "[" + std::to_string(i) + "]", errors);
                }
            }
        }

        static bool check_type(const std::string &type, const json &value)
        {
            if (type == "string")
                return value.is_string();
            if (type == "integer")
                return value.is_number_integer();
            if (type == "number")
                return value.is_number();
            if (type == "boolean")
                return value.is_boolean();
            if (type == "array")
                return value.is_array();
            if (type == "object")
                return value.is_object();
            if (type == "null")
                return value.is_null();
            return true;
        }
    };

} // namespace mcp