#include "test_helpers.h"
#include "mcp/json_rpc/MethodDispatcher.h"
#include "mcp/server/McpMethodHandlers.h"

using namespace mcp;
using json = nlohmann::json;

class ToolsCallTest : public ::testing::Test
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

    void register_echo_tool(
        std::function<ToolResult(const json &, IRequestContext &)> exec)
    {
        handlers->register_tool(ToolDescriptor::make(
            "echo", "Echoes input",
            json{
                {"type", "object"},
                {"properties", {{"text", {{"type", "string"}, {"minLength", 1}}}}},
                {"required", {"text"}}},
            std::move(exec)));
    }

    json make_call(const json &args, json id = 1)
    {
        json params = {
            {"name", "echo"},
            {"arguments", args},
            {"_meta", test::valid_meta()}};
        return test::make_request("tools/call", params, id);
    }

    json dispatch(const json &req)
    {
        auto r = dispatcher.dispatch(req.dump(), writer);
        EXPECT_TRUE(r.has_value());
        return *r;
    }
};

// ---------- 第一层：协议错误 ----------
TEST_F(ToolsCallTest, RejectsMissingMeta)
{
    json params = {{"name", "echo"}, {"arguments", json::object()}};
    auto resp = dispatch(test::make_request("tools/call", params));
    EXPECT_EQ(resp["error"]["code"], -32602);
    std::string msg = resp["error"]["message"].get<std::string>();
    EXPECT_TRUE(msg.find("_meta") != std::string::npos);
    // EXPECT_THAT(resp["error"]["message"].get<std::string>(),
    //             ::testing::HasSubstr("_meta"));
}

TEST_F(ToolsCallTest, RejectsUnknownTool)
{
    json params = {
        {"name", "no_such_tool"},
        {"arguments", json::object()},
        {"_meta", test::valid_meta()}};
    auto resp = dispatch(test::make_request("tools/call", params));
    EXPECT_EQ(resp["error"]["code"], -32602);
    std::string msg = resp["error"]["message"].get<std::string>();
    EXPECT_TRUE(msg.find("Unknown tool") != std::string::npos);

    // EXPECT_THAT(resp["error"]["message"].get<std::string>(),
    //             ::testing::HasSubstr("Unknown tool"));
}

// ---------- 第二层：结构校验 ----------
TEST_F(ToolsCallTest, RejectsSchemaViolation)
{
    register_echo_tool([](const json &, IRequestContext &)
                       { ToolResult r;
                        r.content = json::array();
                        return r; });

    // 缺少 required 的 text 字段
    auto resp = dispatch(make_call(json::object()));
    EXPECT_EQ(resp["error"]["code"], -32602);
    EXPECT_TRUE(resp["error"]["data"].contains("validation_errors"));
}

TEST_F(ToolsCallTest, RejectsWrongArgumentType)
{
    register_echo_tool([](const json &, IRequestContext &)
                       { ToolResult r;
                        r.content = json::array();
                        return r; });

    auto resp = dispatch(make_call(json{{"text", 42}}));
    EXPECT_EQ(resp["error"]["code"], -32602);
}

// ---------- 第三层：业务逻辑 ----------
TEST_F(ToolsCallTest, SuccessfulExecution)
{
    register_echo_tool([](const json &args, IRequestContext &)
                       { ToolResult r;
                        r.content = json::array({{{"type", "text"}, {"text", args["text"]}}});
                        return r; });

    auto resp = dispatch(make_call(json{{"text", "hello"}}));
    EXPECT_TRUE(resp.contains("result"));
    EXPECT_EQ(resp["result"]["resultType"], "complete");
    EXPECT_EQ(resp["result"]["content"][0]["text"], "hello");
    EXPECT_FALSE(resp["result"].contains("isError"));
}

TEST_F(ToolsCallTest, BusinessFailureBecomesIsError)
{
    register_echo_tool([](const json &, IRequestContext &) -> ToolResult
                       { throw std::runtime_error("resource unavailable"); });

    auto resp = dispatch(make_call(json{{"text", "x"}}));
    // 关键：不是 JSON-RPC error，而是 result.isError
    EXPECT_TRUE(resp.contains("result"));
    EXPECT_FALSE(resp.contains("error"));
    EXPECT_EQ(resp["result"]["isError"], true);
    std::string msg = resp["result"]["content"][0]["text"].get<std::string>();
    EXPECT_TRUE(msg.find("resource unavailable") != std::string::npos);
    // EXPECT_THAT(resp["result"]["content"][0]["text"].get<std::string>(),
    //             ::testing::HasSubstr("resource unavailable"));
}

TEST_F(ToolsCallTest, McpExceptionInExecutorBecomesIsError)
{
    register_echo_tool([](const json &, IRequestContext &) -> ToolResult
                       { throw McpException(ErrorCode::InvalidParams,
                                            "semantic rejection"); });

    auto resp = dispatch(make_call(json{{"text", "x"}}));
    // 关键区分：即使业务抛的是 McpException，
    // executor 内部抛出的也应转为 isError，而非 JSON-RPC error
    EXPECT_TRUE(resp.contains("result"));
    EXPECT_EQ(resp["result"]["isError"], true);
}

// ---------- 进度上报贯穿工具执行 ----------
TEST_F(ToolsCallTest, ProgressFlowsThroughToolExecution)
{
    register_echo_tool([](const json &, IRequestContext &ctx)
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

    dispatch(test::make_request("tools/call", params));

    ASSERT_EQ(writer->notifications.size(), 1u);
    EXPECT_EQ(writer->notifications[0]["method"], "notifications/progress");
    EXPECT_EQ(writer->notifications[0]["params"]["progress"], 0.5);
}