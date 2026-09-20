#include "test_helpers.h"
#include "mcp/json_rpc/MethodDispatcher.h"

using namespace mcp;
using namespace mcp::test;
using json = nlohmann::json;

class MethodDispatcherTest : public ::testing::Test
{
protected:
    MethodDispatcher dispatcher;
    std::shared_ptr<test::TestMessageWriter> writer =
        std::make_shared<test::TestMessageWriter>();
};

// ---------- 解析失败与非法输入 ----------
TEST_F(MethodDispatcherTest, ParseErrorOnInvalidJson)
{
    auto resp = dispatcher.dispatch("{not json", writer);
    ASSERT_TRUE(resp);
    EXPECT_EQ((*resp)["error"]["code"], -32700);
    EXPECT_TRUE((*resp)["id"].is_null());
}

TEST_F(MethodDispatcherTest, RejectsBatchRequests)
{
    json batch = json::array({make_request("a", json::object()),
                              make_request("b", json::object())});
    auto resp = dispatcher.dispatch(batch.dump(), writer);
    ASSERT_TRUE(resp);
    EXPECT_EQ((*resp)["error"]["code"], -32600);
    std::string msg = (*resp)["error"]["data"]["detail"].get<std::string>();
    EXPECT_TRUE(msg.find("Batch") != std::string::npos);
    // EXPECT_THAT((*resp)["error"]["message"].get<std::string>(),
    //             ::testing::HasSubstr("Batch"));
}

TEST_F(MethodDispatcherTest, RejectsNonMethodMessage)
{
    json j = {{"jsonrpc", "2.0"}, {"id", 1}, {"result", json::object()}};
    auto resp = dispatcher.dispatch(j.dump(), writer);
    ASSERT_TRUE(resp);
    EXPECT_EQ((*resp)["error"]["code"], -32600);
}

// ---------- 方法未找到 ----------
TEST_F(MethodDispatcherTest, MethodNotFoundWhenUnregistered)
{
    auto resp = dispatcher.dispatch(
        make_request("unknown/method", json::object()).dump(), writer);
    ASSERT_TRUE(resp);
    EXPECT_EQ((*resp)["error"]["code"], -32601);
}

// ---------- 正常分派 ----------
TEST_F(MethodDispatcherTest, InvokesRegisteredHandler)
{
    dispatcher.register_handler("echo",
                                [](const Request &req, IRequestContext &)
                                {
                                    return SuccessResponse::make(req.id, json{{"ok", true}});
                                });

    auto resp = dispatcher.dispatch(
        make_request("echo", json::object(), 7).dump(), writer);
    ASSERT_TRUE(resp);
    EXPECT_EQ((*resp)["id"], 7);
    EXPECT_TRUE((*resp)["result"]["ok"]);
    EXPECT_EQ((*resp)["result"]["resultType"], "complete");
}

TEST_F(MethodDispatcherTest, NotificationReturnsNoResponse)
{
    bool called = false;
    dispatcher.register_notification("notify/me",
                                     [&](const Notification &)
                                     { called = true; });

    json n = {{"jsonrpc", "2.0"}, {"method", "notify/me"}};
    auto resp = dispatcher.dispatch(n.dump(), writer);
    EXPECT_FALSE(resp.has_value());
    EXPECT_TRUE(called);
}

// ---------- logLevel 拒绝 ----------
TEST_F(MethodDispatcherTest, RejectsDeprecatedLogLevel)
{
    json params = {{"_meta", test::valid_meta()}};
    params["_meta"]["io.modelcontextprotocol/logLevel"] = "info";
    dispatcher.register_handler("anything",
                                [](const Request &, IRequestContext &)
                                {
                                    return SuccessResponse::make(RequestId(int64_t{1}), json::object());
                                });

    auto resp = dispatcher.dispatch(
        test::make_request("anything", params).dump(), writer);
    ASSERT_TRUE(resp);
    EXPECT_EQ((*resp)["error"]["code"], -32602);
    std::string msg = (*resp)["error"]["data"]["detail"].get<std::string>();
    EXPECT_TRUE(msg.find("deprecated") != std::string::npos);
}

TEST_F(MethodDispatcherTest, DoesNotRejectRequestWithoutLogLevel)
{
    dispatcher.register_handler("ping",
                                [](const Request &req, IRequestContext &)
                                {
                                    return SuccessResponse::make(req.id, json{{"pong", true}});
                                });

    json params = {{"_meta", valid_meta()}};
    auto resp = dispatcher.dispatch(
        make_request("ping", params).dump(), writer);
    ASSERT_TRUE(resp);
    EXPECT_TRUE((*resp).contains("result"));
}

// ---------- 异常边界 ----------
TEST_F(MethodDispatcherTest, McpExceptionBecomesJsonRpcError)
{
    dispatcher.register_handler("fail",
                                [](const Request &, IRequestContext &) -> std::variant<SuccessResponse, ErrorResponse>
                                {
                                    throw McpException(ErrorCode::InvalidParams, "bad input",
                                                       json{{"detail", "x"}});
                                });

    auto resp = dispatcher.dispatch(
        make_request("fail", json::object()).dump(), writer);
    ASSERT_TRUE(resp);
    EXPECT_EQ((*resp)["error"]["code"], -32602);
    EXPECT_EQ((*resp)["error"]["message"], "bad input");
    EXPECT_EQ((*resp)["error"]["data"]["detail"], "x");
}

TEST_F(MethodDispatcherTest, StdExceptionBecomesInternalError)
{
    dispatcher.register_handler("boom",
                                [](const Request &, IRequestContext &) -> std::variant<SuccessResponse, ErrorResponse>
                                {
                                    throw std::runtime_error("engine failure");
                                });

    auto resp = dispatcher.dispatch(
        make_request("boom", json::object()).dump(), writer);
    ASSERT_TRUE(resp);
    EXPECT_EQ((*resp)["error"]["code"], -32603);
    std::string msg = (*resp)["error"]["data"]["detail"].get<std::string>();
    EXPECT_TRUE(msg.find("engine failure") != std::string::npos);
}

// ---------- 上下文注入 ----------
TEST_F(MethodDispatcherTest, ContextReceivesProgressToken)
{
    std::optional<ProgressToken> captured;
    dispatcher.register_handler("check",
                                [&](const Request &, IRequestContext &ctx)
                                {
                                    captured = ctx.has_progress_token()
                                                   ? std::optional<ProgressToken>(ProgressToken(std::string{"tok"}))
                                                   : std::nullopt;
                                    return SuccessResponse::make(RequestId(int64_t{1}), json::object());
                                });

    json params = {{"_meta", valid_meta()}};
    params["_meta"]["progressToken"] = "tok";

    dispatcher.dispatch(make_request("check", params).dump(), writer);
    ASSERT_TRUE(captured.has_value());
}