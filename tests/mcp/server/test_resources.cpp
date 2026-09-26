#include <gtest/gtest.h>
#include "test_helpers.h"
#include "mcp/server/McpMethodHandlers.h"

using namespace mcp;
using json = nlohmann::json;

class ResourcesTest : public ::testing::Test
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

    void register_static(const std::string &uri,
                         const std::string &content = "static content")
    {
        handlers->register_resource(ResourceDescriptor{
            .uri = uri,
            .name = "Resource " + uri,
            .description = std::nullopt,
            .mime_type = "text/plain",
            .reader = [uri, content]() -> json
            {
                return json::array({{{"uri", uri},
                                     {"mimeType", "text/plain"},
                                     {"text", content}}});
            }});
    }

    void register_template(const std::string &tmpl)
    {
        handlers->register_resource_template(
            ResourceTemplateDescriptor{
                .uri_template = tmpl,
                .name = "Template " + tmpl,
                .description = std::nullopt,
                .mime_type = "text/plain",
                .reader = [](const std::string &uri,
                             const std::unordered_map<std::string,
                                                      std::string> &params)
                    -> json
                {
                    json::object_t captured;
                    for (const auto &[k, v] : params)
                        captured[k] = v;
                    return json::array({{{"uri", uri},
                                         {"mimeType", "text/plain"},
                                         {"text", json(captured).dump()}}});
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
        EXPECT_EQ(r.kind, DispatchOutcome::Kind::Response);
        EXPECT_TRUE(r.payload.has_value());
        return *r.payload;
    }
};

// ============================================================
// resources/list
// ============================================================
TEST_F(ResourcesTest, ListReturnsDeterministicOrder)
{
    register_static("file:///z.txt");
    register_static("file:///a.txt");
    register_static("file:///m.txt");

    auto resp = dispatch(make_request("resources/list", json::object()));
    const auto &arr = resp["result"]["resources"];
    ASSERT_EQ(arr.size(), 3u);
    EXPECT_EQ(arr[0]["uri"], "file:///a.txt");
    EXPECT_EQ(arr[1]["uri"], "file:///m.txt");
    EXPECT_EQ(arr[2]["uri"], "file:///z.txt");
}

TEST_F(ResourcesTest, ListIncludesCacheHints)
{
    register_static("file:///a.txt");
    auto resp = dispatch(make_request("resources/list", json::object()));
    EXPECT_TRUE(resp["result"].contains("ttlMs"));
    EXPECT_TRUE(resp["result"].contains("cacheScope"));
    // 资源权限可能因授权而异，应使用 private
    EXPECT_EQ(resp["result"]["cacheScope"], "private");
}

TEST_F(ResourcesTest, ListIncludesMimeTypeWhenPresent)
{
    register_static("file:///a.txt");
    auto resp = dispatch(make_request("resources/list", json::object()));
    EXPECT_EQ(resp["result"]["resources"][0]["mimeType"], "text/plain");
}

// ============================================================
// resources/templates/list
// ============================================================
TEST_F(ResourcesTest, TemplatesListUsesResourceTemplatesField)
{
    register_template("file:///logs/{date}.log");
    auto resp = dispatch(make_request(
        "resources/templates/list", json::object()));
    // 关键：字段名是 resourceTemplates，不是 resources
    ASSERT_TRUE(resp["result"].contains("resourceTemplates"));
    EXPECT_FALSE(resp["result"].contains("resources"));
    EXPECT_EQ(resp["result"]["resourceTemplates"][0]["uriTemplate"],
              "file:///logs/{date}.log");
}

TEST_F(ResourcesTest, TemplatesListDeterministicOrder)
{
    register_template("file:///z/{id}");
    register_template("file:///a/{id}");

    auto resp = dispatch(make_request(
        "resources/templates/list", json::object()));
    const auto &arr = resp["result"]["resourceTemplates"];
    ASSERT_EQ(arr.size(), 2u);
    EXPECT_EQ(arr[0]["uriTemplate"], "file:///a/{id}");
    EXPECT_EQ(arr[1]["uriTemplate"], "file:///z/{id}");
}

TEST_F(ResourcesTest, TemplatesListIncludesCacheHints)
{
    register_template("file:///logs/{date}.log");
    auto resp = dispatch(make_request(
        "resources/templates/list", json::object()));
    EXPECT_TRUE(resp["result"].contains("ttlMs"));
    EXPECT_EQ(resp["result"]["cacheScope"], "public");
}

// ============================================================
// resources/read —— 静态资源
// ============================================================
TEST_F(ResourcesTest, ReadStaticResource)
{
    register_static("file:///config.json", "{\"port\":8080}");
    auto resp = dispatch(make_request(
        "resources/read",
        json{{"uri", "file:///config.json"}}));

    ASSERT_TRUE(resp.contains("result"));
    EXPECT_EQ(resp["result"]["contents"][0]["uri"],
              "file:///config.json");
    EXPECT_EQ(resp["result"]["contents"][0]["text"],
              "{\"port\":8080}");
}

TEST_F(ResourcesTest, ReadStaticResourceIncludesCacheHints)
{
    register_static("file:///a.txt");
    auto resp = dispatch(make_request(
        "resources/read", json{{"uri", "file:///a.txt"}}));
    EXPECT_TRUE(resp["result"].contains("ttlMs"));
    EXPECT_EQ(resp["result"]["cacheScope"], "private");
}

// ============================================================
// resources/read —— 模板资源
// ============================================================
TEST_F(ResourcesTest, ReadTemplateResource)
{
    register_template("file:///logs/{date}.log");
    auto resp = dispatch(make_request(
        "resources/read",
        json{{"uri", "file:///logs/2024-01-01.log"}}));

    ASSERT_TRUE(resp.contains("result"));
    // reader 返回 captured 参数的 JSON
    std::string text = resp["result"]["contents"][0]["text"];
    auto captured = json::parse(text);
    EXPECT_EQ(captured["date"], "2024-01-01");
}

TEST_F(ResourcesTest, StaticResourceTakesPrecedenceOverTemplate)
{
    // 同时注册静态资源和匹配它的模板
    register_static("file:///logs/today.log", "STATIC");
    register_template("file:///logs/{date}.log");

    auto resp = dispatch(make_request(
        "resources/read",
        json{{"uri", "file:///logs/today.log"}}));

    // 应命中静态资源，返回 "STATIC"
    EXPECT_EQ(resp["result"]["contents"][0]["text"], "STATIC");
}

// ============================================================
// resources/read —— 错误情形
// ============================================================
TEST_F(ResourcesTest, ReadUnknownUriReturnsInvalidParams)
{
    register_static("file:///a.txt");
    auto resp = dispatch(make_request(
        "resources/read", json{{"uri", "file:///nonexistent"}}));
    // MCP 2026-07-28 将 resource-not-found 归入 -32602
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(ResourcesTest, ReadMissingUriReturnsInvalidParams)
{
    auto resp = dispatch(make_request(
        "resources/read", json::object()));
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(ResourcesTest, ReadMissingMetaReturnsInvalidParams)
{
    register_static("file:///a.txt");
    // 不注入 _meta
    auto resp = dispatch(json{
        {"jsonrpc", "2.0"}, {"id", 1}, {"method", "resources/read"}, {"params", {{"uri", "file:///a.txt"}}}});
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(ResourcesTest, ReadUriWithSlashInVariableDoesNotMatch)
{
    register_template("file:///logs/{date}.log");
    // 普通变量不得含 /
    auto resp = dispatch(make_request(
        "resources/read",
        json{{"uri", "file:///logs/2024/01/01.log"}}));
    EXPECT_EQ(resp["error"]["code"], -32602);
}