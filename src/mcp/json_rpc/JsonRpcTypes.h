#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>

namespace mcp
{
    using json = nlohmann::json;

    // ============================================================
    // 协议常量
    // ============================================================
    inline constexpr const char *JSONRPC_VERSION = "2.0";
    inline constexpr const char *MCP_PROTOCOL_VERSION = "2026-07-28";

    // ============================================================
    // MCP 2026-07-28 标准错误码
    // ============================================================
    enum class ErrorCode : int
    {
        ParseError = -32700,
        InvalidRequest = -32600,
        MethodNotFound = -32601,
        InvalidParams = -32602,
        InternalError = -32603,
        // MCP 2026-07-28 新增 / 重新分配
        HeaderMismatch = -32020,             // 请求头与 body 不一致
        UnsupportedProtocolVersion = -32021, // 不支持的协议版本
        // -32022 也用于版本相关错误，可按需定义
    };

    inline std::string to_string(ErrorCode code)
    {
        switch (code)
        {
        case ErrorCode::ParseError:
            return "Parse error";
        case ErrorCode::InvalidRequest:
            return "Invalid Request";
        case ErrorCode::MethodNotFound:
            return "Method not found";
        case ErrorCode::InvalidParams:
            return "Invalid params";
        case ErrorCode::InternalError:
            return "Internal error";
        case ErrorCode::HeaderMismatch:
            return "Header mismatch";
        case ErrorCode::UnsupportedProtocolVersion:
            return "Unsupported protocol version";
        default:
            return "Unknown error";
        }
    }

    // ============================================================
    // ResultType —— MCP 2026-07-28 核心新增
    // ============================================================
    inline constexpr const char *RESULT_TYPE_COMPLETE = "complete";
    inline constexpr const char *RESULT_TYPE_INPUT_REQUIRED = "input_required";

    // ============================================================
    // Request ID —— 必须为 string 或 integer，禁止 null
    // ============================================================
    class RequestId
    {
    public:
        RequestId() = default;
        explicit RequestId(std::string v) : value_(std::move(v)) {}
        explicit RequestId(int64_t v) : value_(v) {}

        static std::optional<RequestId> from_json(const json &j)
        {
            if (j.is_string())
                return RequestId(j.get<std::string>());
            if (j.is_number_integer())
                return RequestId(j.get<int64_t>());
            return std::nullopt; // null / 浮点 / 对象 / 数组均非法
        }

        json to_json() const
        {
            return std::visit([](const auto &v) -> json
                              { return v; }, value_);
        }

        std::string to_string() const { return to_json().dump(); }

        bool operator==(const RequestId &o) const
        {
            return to_json() == o.to_json();
        }

    private:
        std::variant<std::string, int64_t> value_;
    };

    // ============================================================
    // _meta 元数据 —— MCP 2026-07-28 请求中必须携带
    // ============================================================
    struct RequestMeta
    {
        std::string protocol_version = MCP_PROTOCOL_VERSION;
        json client_capabilities = json::object();
        json client_info = json::object();

        // 可选字段
        std::optional<json> progress_token;

        static constexpr const char *KEY_PROTOCOL_VERSION =
            "io.modelcontextprotocol/protocolVersion";
        static constexpr const char *KEY_CLIENT_CAPABILITIES =
            "io.modelcontextprotocol/clientCapabilities";
        static constexpr const char *KEY_CLIENT_INFO =
            "io.modelcontextprotocol/clientInfo";
        static constexpr const char *KEY_LOG_LEVEL =
            "io.modelcontextprotocol/logLevel";

        json to_json() const
        {
            json meta;
            meta[KEY_PROTOCOL_VERSION] = protocol_version;
            meta[KEY_CLIENT_CAPABILITIES] = client_capabilities;
            meta[KEY_CLIENT_INFO] = client_info;
            if (progress_token)
                meta["progressToken"] = *progress_token;
            return meta;
        }

        static std::optional<RequestMeta> from_json(const json &j)
        {
            if (!j.is_object())
                return std::nullopt;
            RequestMeta m;
            if (j.contains(KEY_PROTOCOL_VERSION))
                m.protocol_version = j[KEY_PROTOCOL_VERSION].get<std::string>();
            if (j.contains(KEY_CLIENT_CAPABILITIES))
                m.client_capabilities = j[KEY_CLIENT_CAPABILITIES];
            if (j.contains(KEY_CLIENT_INFO))
                m.client_info = j[KEY_CLIENT_INFO];
            if (j.contains("progressToken"))
                m.progress_token = j["progressToken"];
            return m;
        }

        // 校验三个保留字段是否齐备（MCP 2026-07-28 强制要求）
        bool is_valid() const
        {
            return !protocol_version.empty() && client_capabilities.is_object() && client_info.is_object() && !client_capabilities.empty() && !client_info.empty();
        }
    };

    // ============================================================
    // JSON-RPC Request
    // ============================================================
    struct Request
    {
        RequestId id;
        std::string method;
        std::optional<json> params; // MCP 中 params 必须包含 _meta

        json to_json() const
        {
            json j;
            j["jsonrpc"] = JSONRPC_VERSION;
            j["id"] = id.to_json();
            j["method"] = method;
            if (params)
                j["params"] = *params;
            return j;
        }

        // 解析请求，返回 nullopt 表示格式非法
        static std::optional<Request> from_json(const json &j)
        {
            if (!j.is_object())
                return std::nullopt;
            if (!j.contains("jsonrpc") || j["jsonrpc"] != JSONRPC_VERSION)
                return std::nullopt;
            if (!j.contains("method") || !j["method"].is_string())
                return std::nullopt;
            if (!j.contains("id"))
                return std::nullopt;

            auto rid = RequestId::from_json(j["id"]);
            if (!rid)
                return std::nullopt;

            Request req;
            req.id = std::move(*rid);
            req.method = j["method"].get<std::string>();
            if (j.contains("params"))
                req.params = j["params"];
            return req;
        }

        // 便捷方法：提取并校验 _meta
        std::optional<RequestMeta> extract_meta() const
        {
            if (!params || !params->is_object())
                return std::nullopt;
            if (!params->contains("_meta"))
                return std::nullopt;
            return RequestMeta::from_json((*params)["_meta"]);
        }
    };

    // ============================================================
    // JSON-RPC Notification（无 id，不需要响应）
    // ============================================================
    struct Notification
    {
        std::string method;
        std::optional<json> params;

        json to_json() const
        {
            json j;
            j["jsonrpc"] = JSONRPC_VERSION;
            j["method"] = method;
            if (params)
                j["params"] = *params;
            return j; // 注意：不包含 "id"
        }

        static std::optional<Notification> from_json(const json &j)
        {
            if (!j.is_object())
                return std::nullopt;
            if (!j.contains("jsonrpc") || j["jsonrpc"] != JSONRPC_VERSION)
                return std::nullopt;
            if (!j.contains("method") || !j["method"].is_string())
                return std::nullopt;
            // Notification 不得包含 id
            if (j.contains("id"))
                return std::nullopt;

            Notification n;
            n.method = j["method"].get<std::string>();
            if (j.contains("params"))
                n.params = j["params"];
            return n;
        }
    };

    // ============================================================
    // JSON-RPC Success Response
    // ============================================================
    struct SuccessResponse
    {
        RequestId id;
        json result; // 必须包含 "resultType" 字段

        // 构造时自动注入 resultType（如果调用方未提供）
        static SuccessResponse make(RequestId id, json result_obj)
        {
            if (!result_obj.contains("resultType"))
                result_obj["resultType"] = RESULT_TYPE_COMPLETE;
            return {std::move(id), std::move(result_obj)};
        }

        json to_json() const
        {
            json j;
            j["jsonrpc"] = JSONRPC_VERSION;
            j["id"] = id.to_json();
            j["result"] = result;
            return j;
        }

        static std::optional<SuccessResponse> from_json(const json &j)
        {
            if (!j.is_object())
                return std::nullopt;
            if (!j.contains("jsonrpc") || j["jsonrpc"] != JSONRPC_VERSION)
                return std::nullopt;
            if (!j.contains("id") || !j.contains("result"))
                return std::nullopt;
            // 不能同时包含 result 和 error
            if (j.contains("error"))
                return std::nullopt;

            auto rid = RequestId::from_json(j["id"]);
            if (!rid)
                return std::nullopt;

            return SuccessResponse{std::move(*rid), j["result"]};
        }
    };

    // ============================================================
    // JSON-RPC Error Response
    // ============================================================
    struct ErrorObject
    {
        int code = static_cast<int>(ErrorCode::InternalError);
        std::string message;
        std::optional<json> data;

        json to_json() const
        {
            json j;
            j["code"] = code;
            j["message"] = message;
            if (data)
                j["data"] = *data;
            return j;
        }
    };

    struct ErrorResponse
    {
        // id 在请求无法解析时可以省略
        std::optional<RequestId> id;
        ErrorObject error;

        json to_json() const
        {
            json j;
            j["jsonrpc"] = JSONRPC_VERSION;
            if (id)
                j["id"] = id->to_json();
            else
                j["id"] = nullptr; // 无法确定 id 时返回 null
            j["error"] = error.to_json();
            return j;
        }

        static std::optional<ErrorResponse> from_json(const json &j)
        {
            if (!j.is_object())
                return std::nullopt;
            if (!j.contains("jsonrpc") || j["jsonrpc"] != JSONRPC_VERSION)
                return std::nullopt;
            if (!j.contains("error"))
                return std::nullopt;

            ErrorResponse resp;
            if (j.contains("id") && !j["id"].is_null())
            {
                auto rid = RequestId::from_json(j["id"]);
                if (rid)
                    resp.id = std::move(*rid);
            }
            const auto &e = j["error"];
            resp.error.code = e.value("code", -32603);
            resp.error.message = e.value("message", "Unknown error");
            if (e.contains("data"))
                resp.error.data = e["data"];
            return resp;
        }
    };

    // ============================================================
    // 统一消息类型 —— 任意 JSON-RPC 对象的 variant
    // ============================================================
    struct JsonRpcMessage
    {
        using Variant = std::variant<Request, Notification,
                                     SuccessResponse, ErrorResponse>;
        Variant value;

        json to_json() const
        {
            return std::visit([](const auto &v)
                              { return v.to_json(); }, value);
        }

        // 智能识别消息类型并解析
        static std::optional<JsonRpcMessage> from_json(const json &j)
        {
            if (!j.is_object())
                return std::nullopt;

            // 先判断是否有 id
            bool has_id = j.contains("id") && !j["id"].is_null();

            if (j.contains("method"))
            {
                if (has_id)
                {
                    auto req = Request::from_json(j);
                    if (req)
                        return JsonRpcMessage{std::move(*req)};
                }
                else
                {
                    auto notif = Notification::from_json(j);
                    if (notif)
                        return JsonRpcMessage{std::move(*notif)};
                }
            }
            else if (j.contains("result"))
            {
                auto resp = SuccessResponse::from_json(j);
                if (resp)
                    return JsonRpcMessage{std::move(*resp)};
            }
            else if (j.contains("error"))
            {
                auto err = ErrorResponse::from_json(j);
                if (err)
                    return JsonRpcMessage{std::move(*err)};
            }
            return std::nullopt;
        }
    };

    // ============================================================
    // 辅助：构建 MCP 2026-07-28 请求的快捷函数
    // ============================================================
    inline json build_mcp_params(const json &method_params,
                                 const RequestMeta &meta)
    {
        json params = method_params.is_object() ? method_params : json::object();
        params["_meta"] = meta.to_json();
        return params;
    }

    // 构建错误响应的快捷函数
    inline ErrorResponse make_error(const ErrorObject &err,
                                    std::optional<RequestId> id = std::nullopt)
    {
        return ErrorResponse{std::move(id), err};
    }

    inline ErrorResponse make_error(ErrorCode code,
                                    std::optional<RequestId> id = std::nullopt,
                                    const std::string &detail = "")
    {
        ErrorObject e;
        e.code = static_cast<int>(code);
        e.message = to_string(code);
        if (!detail.empty())
            e.data = json{{"detail", detail}};
        return ErrorResponse{std::move(id), std::move(e)};
    }

    // 业务异常：handler 抛出后由 JsonRpc 转成 error 响应
    class JsonRpcException : public std::runtime_error
    {
    public:
        JsonRpcException(ErrorCode code, std::string msg,
                         std::optional<json> data = std::nullopt)
            : std::runtime_error(msg), code_(code), data_(std::move(data)) {}

        ErrorCode code() const noexcept { return code_; }
        const std::optional<json> &data() const noexcept { return data_; }

    private:
        ErrorCode code_;
        std::optional<json> data_;
    };

    // 空标记类型：表示 handler 已通过 writer 打开了一条长期流。
    // 它不携带数据，仅作为 variant 的一个分支存在。
    struct StreamOpenedTag
    {
    };

    // MCP 领域异常：业务代码抛出此异常，由框架根据调用位置决定错误语义。
    //
    // 在 tools/call 的 executor 内部抛出 → 被捕获并转为 result.isError = true
    // 在协议层/结构层校验中抛出     → 向上冒泡为 JSON-RPC error 对象
    class McpException : public std::runtime_error
    {
    public:
        McpException(ErrorCode code, std::string message,
                     std::optional<json> data = std::nullopt)
            : std::runtime_error(std::move(message)), code_(code), data_(std::move(data)) {}

        ErrorCode code() const noexcept { return code_; }
        const std::optional<json> &data() const noexcept { return data_; }

    private:
        ErrorCode code_;
        std::optional<json> data_;
    };
}
