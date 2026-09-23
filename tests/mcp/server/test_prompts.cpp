#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "test_helpers.h"
#include "mcp/server/McpMethodHandlers.h"

using namespace mcp;
using json = nlohmann::json;

class PromptsTest : public ::testing::Test
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

    void register_code_review()
    {
        handlers->register_prompt(PromptDescriptor{
            .name = "code_review",
            .description = "Review a code snippet",
            .arguments = {
                {"language", "Programming language", true},
                {"code", "Code to review", true},
                {"focus", "Optional focus area", false}},
            .renderer = [](const json &args) -> json
            {
                std::string lang = args["language"];
                std::string code = args["code"];
                std::string focus = args.value("focus", "general quality");
                return json::array({{{"role", "system"},
                                     {"content", {{"type", "text"}, {"text", "Expert " + lang + " reviewer, focus: " + focus}}}},
                                    {{"role", "user"},
                                     {"content", {{"type", "text"}, {"text", "Review:\n" + code}}}}});
            }});
    }

    json make_request(const std::string &method, json params, json id = 1)
    {
        json p = std::move(params);
        if (!p.contains("_meta"))
            p["_meta"] = test::valid_meta();
        return json{
            {"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(p)}};
    }

    json dispatch(const json &req)
    {
        auto r = dispatcher.dispatch(req.dump(), writer);
        EXPECT_TRUE(r.has_value());
        return *r;
    }
};

// ============================================================
// prompts/list
// ============================================================
TEST_F(PromptsTest, ListReturnsDeterministicOrder)
{
    for (const auto &name : {"zebra", "alpha", "middle"})
    {
        handlers->register_prompt(PromptDescriptor{
            .name = name,
            .description = std::nullopt,
            .arguments = {},
            .renderer = [](const json &)
            { return json::array(); }});
    }
    auto resp = dispatch(make_request("prompts/list", json::object()));
    const auto &arr = resp["result"]["prompts"];
    ASSERT_EQ(arr.size(), 3u);
    EXPECT_EQ(arr[0]["name"], "alpha");
    EXPECT_EQ(arr[1]["name"], "middle");
    EXPECT_EQ(arr[2]["name"], "zebra");
}

TEST_F(PromptsTest, ListIncludesArguments)
{
    register_code_review();
    auto resp = dispatch(make_request("prompts/list", json::object()));
    const auto &p = resp["result"]["prompts"][0];
    ASSERT_TRUE(p.contains("arguments"));
    ASSERT_EQ(p["arguments"].size(), 3u);
    EXPECT_EQ(p["arguments"][0]["name"], "language");
    EXPECT_EQ(p["arguments"][0]["required"], true);
    EXPECT_EQ(p["arguments"][2]["name"], "focus");
    EXPECT_EQ(p["arguments"][2]["required"], false);
}

TEST_F(PromptsTest, ListOmitsArgumentsWhenEmpty)
{
    handlers->register_prompt(PromptDescriptor{
        .name = "no_args",
        .description = std::nullopt,
        .arguments = {},
        .renderer = [](const json &)
        { return json::array(); }});
    auto resp = dispatch(make_request("prompts/list", json::object()));
    EXPECT_FALSE(resp["result"]["prompts"][0].contains("arguments"));
}

TEST_F(PromptsTest, ListIncludesCacheHints)
{
    register_code_review();
    auto resp = dispatch(make_request("prompts/list", json::object()));
    EXPECT_TRUE(resp["result"].contains("ttlMs"));
    EXPECT_EQ(resp["result"]["cacheScope"], "public");
}

// ============================================================
// prompts/get —— 成功路径
// ============================================================
TEST_F(PromptsTest, GetRendersMessages)
{
    register_code_review();
    auto resp = dispatch(make_request(
        "prompts/get",
        json{
            {"name", "code_review"},
            {"arguments", {{"language", "python"}, {"code", "print('hi')"}}}}));

    ASSERT_TRUE(resp.contains("result"));
    const auto &msgs = resp["result"]["messages"];
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_EQ(msgs[0]["role"], "system");
    EXPECT_EQ(msgs[1]["role"], "user");
    EXPECT_THAT(msgs[0]["content"]["text"].get<std::string>(),
                ::testing::HasSubstr("python"));
    EXPECT_THAT(msgs[1]["content"]["text"].get<std::string>(),
                ::testing::HasSubstr("print('hi')"));
}

TEST_F(PromptsTest, GetUsesOptionalArgumentWhenProvided)
{
    register_code_review();
    auto resp = dispatch(make_request(
        "prompts/get",
        json{
            {"name", "code_review"},
            {"arguments", {{"language", "rust"}, {"code", "fn main(){}"}, {"focus", "memory safety"}}}}));
    EXPECT_THAT(resp["result"]["messages"][0]["content"]["text"]
                    .get<std::string>(),
                ::testing::HasSubstr("memory safety"));
}

TEST_F(PromptsTest, GetUsesDefaultWhenOptionalArgumentAbsent)
{
    register_code_review();
    auto resp = dispatch(make_request(
        "prompts/get",
        json{
            {"name", "code_review"},
            {"arguments", {{"language", "rust"}, {"code", "fn main(){}"}}}}));
    // focus 未提供时，renderer 内部使用默认值
    EXPECT_THAT(resp["result"]["messages"][0]["content"]["text"]
                    .get<std::string>(),
                ::testing::HasSubstr("general quality"));
}

TEST_F(PromptsTest, GetWithNoArguments)
{
    handlers->register_prompt(PromptDescriptor{
        .name = "greeting",
        .description = std::nullopt,
        .arguments = {},
        .renderer = [](const json &)
        {
            return json::array({{{"role", "user"},
                                 {"content", {{"type", "text"}, {"text", "Hello!"}}}}});
        }});
    auto resp = dispatch(make_request(
        "prompts/get", json{{"name", "greeting"}}));
    ASSERT_TRUE(resp.contains("result"));
    EXPECT_EQ(resp["result"]["messages"][0]["content"]["text"], "Hello!");
}

// ============================================================
// prompts/get —— 错误情形
// ============================================================
TEST_F(PromptsTest, GetRejectsUnknownPrompt)
{
    register_code_review();
    auto resp = dispatch(make_request(
        "prompts/get", json{{"name", "nonexistent"}}));
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(PromptsTest, GetRejectsMissingRequiredArgument)
{
    register_code_review();
    auto resp = dispatch(make_request(
        "prompts/get",
        json{
            {"name", "code_review"},
            // 缺少 code
            {"arguments", {{"language", "python"}}}}));
    EXPECT_EQ(resp["error"]["code"], -32602);
    EXPECT_THAT(resp["error"]["message"].get<std::string>(),
                ::testing::HasSubstr("code"));
}

TEST_F(PromptsTest, GetRejectsMissingName)
{
    register_code_review();
    auto resp = dispatch(make_request(
        "prompts/get", json{{"arguments", json::object()}}));
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(PromptsTest, GetRejectsMissingMeta)
{
    register_code_review();
    auto resp = dispatch(json{
        {"jsonrpc", "2.0"}, {"id", 1}, {"method", "prompts/get"}, {"params", {{"name", "code_review"}, {"arguments", {{"language", "python"}, {"code", "x"}}}}}});
    EXPECT_EQ(resp["error"]["code"], -32602);
}