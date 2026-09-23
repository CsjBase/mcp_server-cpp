#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "test_helpers.h"
#include "mcp/server/McpMethodHandlers.h"

using namespace mcp;
using json = nlohmann::json;

class ToolsTest : public ::testing::Test
{
protected:
    MethodDispatcher dispatcher;
    std::unique_ptr<McpMethodHandlers> handlers;
    std::shared_ptr<test::TestMessageWriter> writer =
        std::make_shared<test::TestMessageWriter>();

    void SetUp() override
    {
        handlers = std::make_unique<McpMethodHandlers>(dispatcher);
    }

    void register_echo(
        std::function<ToolResult(const json &,
                                 IRequestContext &)>
            exec =
                [](const json &args, IRequestContext &)
        {
            ToolResult r;
            r.content = json::array({{{"type", "text"}, {"text", args.value("text", "")}}});
            return r;
        },
        std::optional<json> output_schema = std::nullopt)
    {
        handlers->register_tool(ToolDescriptor::make(
            "echo", "Echo the input",
            json{
                {"type", "object"},
                {"properties", {{"text", {{"type", "string"}, {"minLength", 1}}}}},
                {"required", {"text"}}},
            std::move(exec),
            std::move(output_schema)));
    }

    json make_request(const std::string &method, json params,
                      json id = 1)
    {
        json p = std::move(params);
        if (!p.contains("_meta"))
            p["_meta"] = test::valid_meta();
        return json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"method", method},
            {"params", std::move(p)}};
    }

    json dispatch(const json &req)
    {
        auto r = dispatcher.dispatch(req.dump(), writer);
        EXPECT_TRUE(r.has_value());
        return *r;
    }
};

// ============================================================
// tools/list
// ============================================================
TEST_F(ToolsTest, ToolsListReturnsDeterministicOrder)
{
    // 故意逆序注册
    handlers->register_tool(ToolDescriptor::make(
        "zebra", "Z", json{{"type", "object"}},
        [](const json &, IRequestContext &)
        {
            return ToolResult{};
        }));
    handlers->register_tool(ToolDescriptor::make(
        "alpha", "A", json{{"type", "object"}},
        [](const json &, IRequestContext &)
        {
            return ToolResult{};
        }));
    handlers->register_tool(ToolDescriptor::make(
        "middle", "M", json{{"type", "object"}},
        [](const json &, IRequestContext &)
        {
            return ToolResult{};
        }));

    auto resp = dispatch(make_request("tools/list", json::object()));
    ASSERT_TRUE(resp.contains("result"));
    const auto &tools = resp["result"]["tools"];
    ASSERT_EQ(tools.size(), 3u);
    EXPECT_EQ(tools[0]["name"], "alpha");
    EXPECT_EQ(tools[1]["name"], "middle");
    EXPECT_EQ(tools[2]["name"], "zebra");
}

TEST_F(ToolsTest, ToolsListIncludesCacheHints)
{
    register_echo();
    auto resp = dispatch(make_request("tools/list", json::object()));
    EXPECT_TRUE(resp["result"].contains("ttlMs"));
    EXPECT_TRUE(resp["result"].contains("cacheScope"));
    EXPECT_GT(resp["result"]["ttlMs"].get<int>(), 0);
    EXPECT_EQ(resp["result"]["cacheScope"], "public");
}

TEST_F(ToolsTest, ToolsListIncludesInputSchema)
{
    register_echo();
    auto resp = dispatch(make_request("tools/list", json::object()));
    const auto &tool = resp["result"]["tools"][0];
    EXPECT_TRUE(tool.contains("inputSchema"));
    EXPECT_EQ(tool["inputSchema"]["type"], "object");
}

TEST_F(ToolsTest, ToolsListIncludesOutputSchemaWhenDeclared)
{
    register_echo(
        [](const json &, IRequestContext &)
        {
            return ToolResult{};
        },
        json{{"type", "object"},
             {"properties", {{"echoed", {{"type", "string"}}}}}});

    auto resp = dispatch(make_request("tools/list", json::object()));
    const auto &tool = resp["result"]["tools"][0];
    ASSERT_TRUE(tool.contains("outputSchema"));
    EXPECT_EQ(tool["outputSchema"]["type"], "object");
}

TEST_F(ToolsTest, ToolsListOmitsOutputSchemaWhenAbsent)
{
    register_echo();
    auto resp = dispatch(make_request("tools/list", json::object()));
    EXPECT_FALSE(resp["result"]["tools"][0].contains("outputSchema"));
}

TEST_F(ToolsTest, ToolsListRejectsPaginationCursor)
{
    register_echo();
    auto req = make_request("tools/list", json{{"cursor", "abc"}});
    auto resp = dispatch(req);
    EXPECT_EQ(resp["error"]["code"], -32602);
}

// ============================================================
// tools/call —— 协议层校验
// ============================================================
TEST_F(ToolsTest, CallRejectsMissingMeta)
{
    register_echo();
    json params = {{"name", "echo"}, {"arguments", {{"text", "x"}}}};
    // 不注入 _meta
    auto resp = dispatch(json{
        {"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"}, {"params", params}});
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(ToolsTest, CallRejectsMissingName)
{
    register_echo();
    auto resp = dispatch(make_request(
        "tools/call", json{{"arguments", {{"text", "x"}}}}));
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(ToolsTest, CallRejectsUnknownTool)
{
    register_echo();
    auto resp = dispatch(make_request(
        "tools/call",
        json{{"name", "unknown_tool"}, {"arguments", json::object()}}));
    EXPECT_EQ(resp["error"]["code"], -32602);
    EXPECT_THAT(resp["error"]["message"].get<std::string>(),
                ::testing::HasSubstr("Unknown tool"));
}

// ============================================================
// tools/call —— 结构层校验
// ============================================================
TEST_F(ToolsTest, CallRejectsMissingRequiredArgument)
{
    register_echo();
    auto resp = dispatch(make_request(
        "tools/call",
        json{{"name", "echo"}, {"arguments", json::object()}}));
    EXPECT_EQ(resp["error"]["code"], -32602);
    ASSERT_TRUE(resp["error"].contains("data"));
    EXPECT_TRUE(resp["error"]["data"].contains("validation_errors"));
}

TEST_F(ToolsTest, CallRejectsWrongArgumentType)
{
    register_echo();
    auto resp = dispatch(make_request(
        "tools/call",
        json{{"name", "echo"}, {"arguments", {{"text", 42}}}}));
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(ToolsTest, CallRejectsEmptyStringViolatingMinLength)
{
    register_echo();
    auto resp = dispatch(make_request(
        "tools/call",
        json{{"name", "echo"}, {"arguments", {{"text", ""}}}}));
    EXPECT_EQ(resp["error"]["code"], -32602);
}

// ============================================================
// tools/call —— 成功路径
// ============================================================
TEST_F(ToolsTest, CallReturnsContent)
{
    register_echo();
    auto resp = dispatch(make_request(
        "tools/call",
        json{{"name", "echo"}, {"arguments", {{"text", "hello"}}}}));
    ASSERT_TRUE(resp.contains("result"));
    EXPECT_EQ(resp["result"]["resultType"], "complete");
    EXPECT_EQ(resp["result"]["content"][0]["text"], "hello");
    EXPECT_FALSE(resp["result"].contains("isError"));
}

TEST_F(ToolsTest, CallReturnsStructuredContentWhenProvided)
{
    register_echo([](const json &args, IRequestContext &)
                  {
        ToolResult r;
        r.content = json::array({
            {{"type", "text"}, {"text", "ok"}}
        });
        r.structured_content = json{
            {"echoed", args["text"]},
            {"length", args["text"].get<std::string>().size()}
        };
        return r; });

    auto resp = dispatch(make_request(
        "tools/call",
        json{{"name", "echo"}, {"arguments", {{"text", "abc"}}}}));
    ASSERT_TRUE(resp["result"].contains("structuredContent"));
    EXPECT_EQ(resp["result"]["structuredContent"]["echoed"], "abc");
    EXPECT_EQ(resp["result"]["structuredContent"]["length"], 3);
}

// ============================================================
// tools/call —— 语义层失败 → isError
// ============================================================
TEST_F(ToolsTest, BusinessFailureReturnsIsError)
{
    register_echo([](const json &, IRequestContext &) -> ToolResult
                  { throw std::runtime_error("resource unavailable"); });

    auto resp = dispatch(make_request(
        "tools/call",
        json{{"name", "echo"}, {"arguments", {{"text", "x"}}}}));
    // 关键：是 result.isError，不是 JSON-RPC error
    EXPECT_TRUE(resp.contains("result"));
    EXPECT_FALSE(resp.contains("error"));
    EXPECT_EQ(resp["result"]["isError"], true);
    EXPECT_THAT(resp["result"]["content"][0]["text"].get<std::string>(),
                ::testing::HasSubstr("resource unavailable"));
}

TEST_F(ToolsTest, McpExceptionInExecutorBecomesIsError)
{
    register_echo([](const json &, IRequestContext &) -> ToolResult
                  { throw McpException(ErrorCode::InvalidParams,
                                       "semantic rejection"); });

    auto resp = dispatch(make_request(
        "tools/call",
        json{{"name", "echo"}, {"arguments", {{"text", "x"}}}}));
    // executor 内部抛出的任何异常都转为 isError
    EXPECT_TRUE(resp.contains("result"));
    EXPECT_EQ(resp["result"]["isError"], true);
}

// ============================================================
// tools/call —— 进度上报
// ============================================================
TEST_F(ToolsTest, ProgressFlowsThroughToolExecution)
{
    register_echo([](const json &, IRequestContext &ctx)
                  {
        ctx.report_progress(0.5, 1.0, "halfway");
        ToolResult r;
        r.content = json::array({{{"type", "text"}, {"text", "done"}}});
        return r; });

    json params = {
        {"name", "echo"},
        {"arguments", {{"text", "x"}}},
        {"_meta", test::valid_meta()}};
    params["_meta"]["progressToken"] = "p1";

    dispatch(json{
        {"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"}, {"params", params}});

    ASSERT_EQ(writer->notifications.size(), 1u);
    EXPECT_EQ(writer->notifications[0]["method"], "notifications/progress");
    EXPECT_EQ(writer->notifications[0]["params"]["progress"], 0.5);
}