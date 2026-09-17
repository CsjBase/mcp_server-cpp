#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <iostream>
#include <shared_mutex>
#include <memory>

namespace mcp
{
    using json = nlohmann::json;
    struct JsonRpcError
    {
        int code;
        std::string message;
        std::optional<json> data;
    };

    struct JsonRpcRequest
    {
        std::optional<std::string> jsonrpc;
        std::optional<json> id; // 缺省表示 Notification（无响应）
        std::optional<std::string> method;
        std::optional<json> params; // 对象或数组，缺省表示无参数
    };

    struct JsonRpcResponse
    {
        std::string jsonrpc;
        json id; // 允许字符串/数字/null
        std::optional<json> result;
        std::optional<JsonRpcError> error;
    };

    // 序列化
    void to_json(json &j, const JsonRpcError &e);
    void from_json(const json &j, JsonRpcError &e);
    void to_json(json &j, const JsonRpcRequest &r);
    void from_json(const json &j, JsonRpcRequest &r);
    void to_json(json &j, const JsonRpcResponse &r);
    void from_json(const json &j, JsonRpcResponse &r);

    class InvalidParams : std::exception
    {
    public:
        explicit InvalidParams(std::string m, std::optional<json> d = std::nullopt)
            : msg(std::move(m)), data(std::move(d)) {}
        const char *what() const noexcept override { return msg.c_str(); }
        std::optional<json> getDate() const { return data; }

    private:
        std::string msg;
        std::optional<json> data;
    };

    class InvalidRequest : std::exception
    {
    public:
        explicit InvalidRequest(std::string m, std::optional<json> d = std::nullopt)
            : msg(std::move(m)), data(std::move(d)) {}
        const char *what() const noexcept override { return msg.c_str(); }
        std::optional<json> getDate() const { return data; }

    private:
        std::string msg;
        std::optional<json> data;
    };

    class JsonRpcMethodDispatcher
    {
    public:
        using ptr = std::unique_ptr<JsonRpcMethodDispatcher>;
        using ParamValidator = std::function<void(const json &params)>;
        using MethodCallback = std::function<json(const json &params)>;
        struct MethodEntry
        {
            ParamValidator validator; // 可为空
            MethodCallback callback;
        };

        JsonRpcMethodDispatcher() = default;
        JsonRpcMethodDispatcher(const JsonRpcMethodDispatcher &) = delete;
        JsonRpcMethodDispatcher &operator=(const JsonRpcMethodDispatcher &) = delete;

        void registerHandler(const std::string &method, MethodEntry handler);
        void unregisterHandler(const std::string &method);
        bool hasHandler(const std::string &method) const;

        std::optional<JsonRpcResponse> handleRequest(const JsonRpcRequest &req);

    private:
        mutable std::shared_mutex mutex_;
        std::unordered_map<std::string, MethodEntry> methods_;
    };

    class JsonRpc
    {
    public:
        using ptr = std::unique_ptr<JsonRpc>;
        explicit JsonRpc(JsonRpcMethodDispatcher::ptr dispatcher)
            : dispatcher_(std::move(dispatcher))
        {
        }
        std::optional<std::string> dispatch(const std::string &raw);
        std::optional<json> dispatch(const json &parsed);

    private:
        std::optional<JsonRpcResponse> dispatchSingle(const json &req);
        std::optional<std::vector<JsonRpcResponse>> dispatchBatch(const json &arr);
        void verifyRequest(const JsonRpcRequest &req);

    private:
        JsonRpcMethodDispatcher::ptr dispatcher_;
    };

    // 常用错误码
    namespace jsonrpc_errc
    {
        constexpr int ParseError = -32700;
        constexpr int InvalidRequest = -32600;
        constexpr int MethodNotFound = -32601;
        constexpr int InvalidParams = -32602;
        constexpr int InternalError = -32603;
        // 应用自定义错误建议使用 -32000 ~ -32099
    }

}