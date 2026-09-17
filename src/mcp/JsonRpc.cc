#include "JsonRpc.h"
#include "Log.h"

#include <mutex>

namespace mcp
{
    // JsonRpcError
    void to_json(json &j, const JsonRpcError &e)
    {
        j = json{{"code", e.code}, {"message", e.message}};
        if (e.data.has_value())
        {
            j["data"] = *e.data;
        }
    }

    void from_json(const json &j, JsonRpcError &e)
    {
        e.code = j.at("code").get<int>();
        e.message = j.at("message").get<std::string>();
        if (j.contains("data"))
        {
            e.data = j.at("data");
        }
        else
        {
            e.data.reset();
        }
    }

    // JsonRpcRequest
    void to_json(json &j, const JsonRpcRequest &r)
    {
        j = json::object();

        // jsonrpc：有值才写（但正常情况下一定是 "2.0"）
        if (r.jsonrpc.has_value())
        {
            j["jsonrpc"] = *r.jsonrpc;
        }

        // method：有值才写
        if (r.method.has_value())
        {
            j["method"] = *r.method;
        }

        // id：有值才写
        if (r.id.has_value())
        {
            j["id"] = *r.id;
        }

        // params：有值才写
        if (r.params.has_value())
        {
            j["params"] = *r.params;
        }
    }

    void from_json(const json &j, JsonRpcRequest &r)
    {
        r = JsonRpcRequest{}; // 重置，防止复用脏数据

        if (!j.is_object())
        {
            return; // 不是对象，全部保持 nullopt，交给 Dispatcher 判
        }

        // jsonrpc：存在且为字符串才填
        if (auto it = j.find("jsonrpc"); it != j.end() && it->is_string())
        {
            r.jsonrpc = it->get<std::string>();
        }

        // method：存在且为字符串才填
        if (auto it = j.find("method"); it != j.end() && it->is_string())
        {
            r.method = it->get<std::string>();
        }

        // id：存在就填，任意类型（规范允许 string/number/null）
        if (auto it = j.find("id"); it != j.end())
        {
            r.id = *it;
        }

        // params：存在就填
        if (auto it = j.find("params"); it != j.end())
        {
            r.params = *it;
        }
    }

    // JsonRpcResponse
    void to_json(json &j, const JsonRpcResponse &r)
    {
        j = json{{"jsonrpc", r.jsonrpc}, {"id", r.id}};

        // Enforce mutual exclusivity per JSON-RPC 2.0
        if (r.error.has_value())
        {
            j["error"] = *r.error;
        }
        else if (r.result.has_value())
        {
            j["result"] = *r.result;
        }
    }

    void from_json(const json &j, JsonRpcResponse &r)
    {
        r.jsonrpc = j.at("jsonrpc").get<std::string>();
        r.id = j.at("id");

        // Only one of result or error may be present
        if (j.contains("error"))
        {
            r.error = j.at("error").get<JsonRpcError>();
            r.result.reset();
        }
        else if (j.contains("result"))
        {
            r.result = j.at("result");
            r.error.reset();
        }
        else
        {
            r.result.reset();
            r.error.reset();
        }
    }

    void JsonRpcMethodDispatcher::registerHandler(const std::string &method, MethodEntry handler)
    {
        std::unique_lock lock(mutex_);
        methods_[method] = std::move(handler);
    }

    void JsonRpcMethodDispatcher::unregisterHandler(const std::string &method)
    {
        std::unique_lock lock(mutex_);
        methods_.erase(method);
    }

    bool JsonRpcMethodDispatcher::hasHandler(const std::string &method) const
    {
        std::shared_lock lock(mutex_);
        return methods_.find(method) != methods_.end();
    }

    std::optional<JsonRpcResponse> JsonRpcMethodDispatcher::handleRequest(const JsonRpcRequest &req)
    {
        MethodEntry entry;
        bool isNotification = !req.id.has_value();
        {
            std::shared_lock lock(mutex_);
            auto it = methods_.find(req.method.value());
            if (it == methods_.end())
            {
                JsonRpcResponse rsp;
                rsp.id = req.id;
                rsp.jsonrpc = req.jsonrpc.value();
                rsp.error = JsonRpcError{jsonrpc_errc::MethodNotFound, "Method not found", std::nullopt};
                return rsp;
            }
            entry = it->second;
        }
        try
        {
            entry.validator(req.params.value_or(json::object()));
            if (!isNotification)
            {
                JsonRpcResponse rsp;
                rsp.result = entry.callback(req.params.value_or(json::object()));
                rsp.id = req.id;
                rsp.jsonrpc = req.jsonrpc.value();
                return rsp;
            }
            else
            {
                entry.callback(req);
                return std::nullopt;
            }
        }
        catch (const InvalidParams &e)
        {
            MCP_LOG_WARN("Invalid params: {}", e.what());
            if (!isNotification)
            {
                JsonRpcResponse rsp;
                rsp.id = req.id;
                rsp.jsonrpc = req.jsonrpc.value();
                rsp.error = JsonRpcError{jsonrpc_errc::InvalidParams, e.what(), e.getDate()};
                return rsp;
            }
            else
            {
                return std::nullopt;
            }
        }
        catch (const std::exception &e)
        {
            MCP_LOG_ERROR("Method handle internal error: {}", e.what());
            if (!isNotification)
            {
                JsonRpcResponse rsp;
                rsp.id = req.id;
                rsp.jsonrpc = req.jsonrpc.value();
                rsp.error = JsonRpcError{jsonrpc_errc::InternalError, "Method handle internal error", std::nullopt};
                return rsp;
            }
            else
            {
                return std::nullopt;
            }
        }
    }

    std::optional<std::string> JsonRpc::dispatch(const std::string &raw)
    {
        json parsed = json::parse(raw, nullptr, false);
        if (parsed.is_discarded())
        {
            MCP_LOG_WARN("JSON parse error");
            json rsp;
            rsp["jsonrpc"] = "2.0";
            rsp["error"] = json{{"code", jsonrpc_errc::ParseError},
                                {"message", "Parse error: bad JSON string"}};
            return rsp.dump();
        }
        try
        {
            auto result = dispatch(parsed);
            if (result.has_value())
            {
                return result.value().dump();
            }
            return std::nullopt;
        }
        catch (const std::exception &e)
        {
            MCP_LOG_ERROR("JsonRpc dispatch internal error: {}", e.what());
            json rsp;
            rsp["jsonrpc"] = "2.0";
            rsp["error"] = json{{"code", jsonrpc_errc::InternalError},
                                {"message", "JSON-RPC dispatch internal error"}};
            return rsp.dump();
        }
    }

    std::optional<json> JsonRpc::dispatch(const json &parsed)
    {
        if (parsed.is_array())
        {
            if (parsed.empty())
            {
                MCP_LOG_WARN("Invalid Request: empty batch");
                return JsonRpcResponse{"2.0",
                                       nullptr,
                                       std::nullopt,
                                       JsonRpcError{jsonrpc_errc::InvalidRequest,
                                                    "Invalid Request: empty batch",
                                                    std::nullopt}};
            }
            auto result = dispatchBatch(parsed);
            if (!result.has_value())
            {
                return std::nullopt;
            }
            return json(result);
        }
        else
        {
            auto result = dispatchSingle(parsed);
            if (!result.has_value())
            {
                return std::nullopt;
            }
            return json(result);
        }
    }

    std::optional<JsonRpcResponse> JsonRpc::dispatchSingle(const json &req)
    {
        if (!req.is_object())
        {
            MCP_LOG_WARN("Not a JSON object");
            return JsonRpcResponse{"2.0",
                                   nullptr,
                                   std::nullopt,
                                   JsonRpcError{jsonrpc_errc::InvalidRequest,
                                                "Invalid Request: not a json object",
                                                std::nullopt}};
        }
        JsonRpcRequest r = req.get<JsonRpcRequest>();
        json id = r.id.value_or(json(nullptr));
        try
        {
            verifyRequest(r);
            return dispatcher_->handleRequest(r);
        }
        catch (const InvalidRequest &e)
        {
            MCP_LOG_WARN("Invalid Request: {}", e.what());
            return JsonRpcResponse{"2.0",
                                   id,
                                   std::nullopt,
                                   JsonRpcError{jsonrpc_errc::InvalidRequest,
                                                e.what(),
                                                e.getDate()}};
        }
        catch (const std::exception &e)
        {
            MCP_LOG_ERROR("JsonRpc dispatch internal error: {}", e.what());
            return JsonRpcResponse{"2.0",
                                   id,
                                   std::nullopt,
                                   JsonRpcError{jsonrpc_errc::InternalError,
                                                "JSON-RPC dispatch internal error",
                                                std::nullopt}};
        }
    }

    std::optional<std::vector<JsonRpcResponse>> JsonRpc::dispatchBatch(const json &arr)
    {
        std::vector<JsonRpcResponse> result;
        for (auto &req : arr)
        {
            auto r = dispatchSingle(req);
            if (r.has_value())
            {
                result.push_back(std::move(*r));
            }
        }
        if (result.empty())
        {
            return std::nullopt;
        }
        return result;
    }
    void JsonRpc::verifyRequest(const JsonRpcRequest &req)
    {
        if (!req.jsonrpc.has_value())
        {
            throw InvalidRequest("Invalid Request: missing jsonrpc");
        }
        if (req.jsonrpc.value() != "2.0")
        {
            throw InvalidRequest("Invalid Request: bad jsonrpc version");
        }
        if (!req.method.has_value())
        {
            throw InvalidRequest("Invalid Request: missing method");
        }
    }
}