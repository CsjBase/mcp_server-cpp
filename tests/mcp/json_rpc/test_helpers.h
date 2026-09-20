#pragma once

#include "mcp/json_rpc/JsonRpcTypes.h"
#include "mcp/json_rpc/JsonRpcContext.h"

#include <vector>
#include <gtest/gtest.h>

namespace mcp::test
{

    using json = nlohmann::json;

    // ---- Mock IMessageWriter：记录所有写入，供断言检查 ----
    class TestMessageWriter : public mcp::IMessageWriter
    {
    public:
        std::vector<json> notifications;
        std::vector<json> responses;
        bool streaming = true;

        void write_notification(const json &n) override
        {
            std::cerr << "write_notification: " << n.dump() << std::endl;
            notifications.push_back(n);
        }
        void write_response(const json &r) override
        {
            std::cerr << "write_response: " << r.dump() << std::endl;
            responses.push_back(r);
        }
        bool is_streaming() const override { return streaming; }

        void clear()
        {
            notifications.clear();
            responses.clear();
        }
    };

    // ---- 构造完整有效的 _meta ----
    inline json valid_meta()
    {
        return json{
            {"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
            {"io.modelcontextprotocol/clientInfo", {{"name", "test"}, {"version", "1"}}},
            {"io.modelcontextprotocol/clientCapabilities", {{"tools", true}}}};
    }

    // ---- 构造带 _meta 的请求 ----
    inline json make_request(const std::string &method, const json &params,
                             json id = 1)
    {
        return json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"method", method},
            {"params", params}};
    }

} // namespace mcp::test