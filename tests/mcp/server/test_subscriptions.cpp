#include <gtest/gtest.h>
#include "test_helpers.h"
#include "mcp/server/McpMethodHandlers.h"
#include "mcp/json_rpc/MethodDispatcher.h"

using namespace mcp;
using json = nlohmann::json;
using DispatchOutcome = DispatchOutcome;

// ============================================================
// 测试夹具
// ============================================================
class SubscriptionsTest : public ::testing::Test
{
protected:
    MethodDispatcher dispatcher;
    std::unique_ptr<McpMethodHandlers> handlers;
    std::shared_ptr<test::TestMessageWriter> writer =
        std::make_shared<test::TestMessageWriter>();

    void SetUp() override
    {
        handlers = std::make_unique<McpMethodHandlers>(
            dispatcher);
    }

    // 构造 listen 请求
    json make_listen_request(json notifications,
                             json id = "sub-1",
                             bool include_meta = true)
    {
        json params = json::object();
        if (!notifications.is_null())
        {
            params["notifications"] = std::move(notifications);
        }
        if (include_meta)
        {
            params["_meta"] = test::valid_meta();
        }
        return json{
            {"jsonrpc", "2.0"},
            {"id", std::move(id)},
            {"method", "subscriptions/listen"},
            {"params", std::move(params)}};
    }

    DispatchOutcome dispatch(const json &req)
    {
        return dispatcher.dispatch(req.dump(), writer);
    }

    // 便捷：取出 writer 收到的第一条通知
    const json &first_notification() const
    {
        EXPECT_FALSE(writer->notifications.empty());
        return writer->notifications[0];
    }

    // 便捷：取出 acknowledged 通知中的 notifications 字段
    json ack_notifications() const
    {
        const auto &n = first_notification();
        return n["params"]["notifications"];
    }

    // 便捷：取出 acknowledged 通知中的 subscriptionId
    std::string ack_subscription_id() const
    {
        const auto &n = first_notification();
        return n["params"]["_meta"]
                ["io.modelcontextprotocol/subscriptionId"]
                    .get<std::string>();
    }
};

// ============================================================
// 基本行为：返回 StreamOpened，ack 作为第一条消息
// ============================================================
TEST_F(SubscriptionsTest, ReturnsStreamOpened)
{
    auto outcome = dispatch(make_listen_request(
        json{{"toolsListChanged", true}}));

    EXPECT_EQ(outcome.kind, DispatchOutcome::Kind::StreamOpened);
    EXPECT_FALSE(outcome.payload.has_value());
}

TEST_F(SubscriptionsTest, AcknowledgedIsFirstNotification)
{
    dispatch(make_listen_request(json{{"toolsListChanged", true}}));

    ASSERT_EQ(writer->notifications.size(), 1u);
    const auto &ack = first_notification();
    EXPECT_EQ(ack["jsonrpc"], "2.0");
    EXPECT_EQ(ack["method"],
              "notifications/subscriptions/acknowledged");
}

TEST_F(SubscriptionsTest, AcknowledgedHasNoResponseId)
{
    // acknowledged 是通知，不是响应，不应携带顶层 id
    dispatch(make_listen_request(json{{"toolsListChanged", true}}));
    EXPECT_FALSE(first_notification().contains("id"));
}

TEST_F(SubscriptionsTest, AcknowledgedCarriesSubscriptionIdInMeta)
{
    dispatch(make_listen_request(
        json{{"toolsListChanged", true}}, "my-sub-id"));

    // subscriptionId 是字符串形式，等于 listen 请求的 id
    EXPECT_EQ(ack_subscription_id(), "\"my-sub-id\"");
}

TEST_F(SubscriptionsTest, AcknowledgedCarriesIntegerSubscriptionId)
{
    dispatch(make_listen_request(
        json{{"toolsListChanged", true}}, 42));

    // 整数 id 序列化后是 "42"
    EXPECT_EQ(ack_subscription_id(), "42");
}

// ============================================================
// 过滤器协商：冻结架构下不推送 list_changed
// ============================================================
TEST_F(SubscriptionsTest, FrozenRegistryOmitsListChangedFlags)
{
    // 请求所有 list_changed 类型，但注册表已冻结，
    // 服务端不会推送任何变更，因此 acknowledged 中不包含这些字段
    auto outcome = dispatch(make_listen_request(json{
        {"toolsListChanged", true},
        {"promptsListChanged", true},
        {"resourcesListChanged", true}}));

    EXPECT_EQ(outcome.kind, DispatchOutcome::Kind::StreamOpened);
    auto ack = ack_notifications();
    EXPECT_FALSE(ack.contains("toolsListChanged"));
    EXPECT_FALSE(ack.contains("promptsListChanged"));
    EXPECT_FALSE(ack.contains("resourcesListChanged"));
}

TEST_F(SubscriptionsTest, EmptyFilterStillAcknowledged)
{
    // 空过滤器：客户端什么都不订阅，ack 仍应发送，
    // notifications 字段为空对象
    auto outcome = dispatch(make_listen_request(json::object()));

    EXPECT_EQ(outcome.kind, DispatchOutcome::Kind::StreamOpened);
    auto ack = ack_notifications();
    EXPECT_TRUE(ack.is_object());
    EXPECT_TRUE(ack.empty());
}

TEST_F(SubscriptionsTest, MissingNotificationsFieldIsTreatedAsEmpty)
{
    // params 中没有 notifications 字段
    auto outcome = dispatch(make_listen_request(json(nullptr)));

    EXPECT_EQ(outcome.kind, DispatchOutcome::Kind::StreamOpened);
    EXPECT_TRUE(ack_notifications().empty());
}

// ============================================================
// 资源订阅
// ============================================================
TEST_F(SubscriptionsTest, ResourceSubscriptionHonoredForExistingStatic)
{
    handlers->register_resource(ResourceDescriptor{
        .uri = "file:///a.txt",
        .name = "A",
        .description = std::nullopt,
        .mime_type = "text/plain",
        .reader = []()
        { return json::array(); }});

    dispatch(make_listen_request(json{
        {"resourceSubscriptions", {"file:///a.txt"}}}));

    auto ack = ack_notifications();
    ASSERT_TRUE(ack.contains("resourceSubscriptions"));
    ASSERT_EQ(ack["resourceSubscriptions"].size(), 1u);
    EXPECT_EQ(ack["resourceSubscriptions"][0], "file:///a.txt");
}

TEST_F(SubscriptionsTest, ResourceSubscriptionHonoredForTemplate)
{
    handlers->register_resource_template(
        ResourceTemplateDescriptor{
            .uri_template = "file:///logs/{date}.log",
            .name = "Log",
            .description = std::nullopt,
            .mime_type = "text/plain",
            .reader = [](const std::string &uri,
                         const std::unordered_map<std::string, std::string> &params)
            {
                return json::array();
            }});

    dispatch(make_listen_request(json{
        {"resourceSubscriptions", {"file:///logs/2024-01-01.log"}}}));

    auto ack = ack_notifications();
    std::cout << ack.dump() << std::endl;
    ASSERT_TRUE(ack.contains("resourceSubscriptions"));
    EXPECT_EQ(ack["resourceSubscriptions"][0],
              "file:///logs/2024-01-01.log");
}

TEST_F(SubscriptionsTest, ResourceSubscriptionOmittedForUnknownUri)
{
    // 请求订阅不存在的 URI，服务端不应同意
    dispatch(make_listen_request(json{
        {"resourceSubscriptions", {"file:///nonexistent"}}}));

    auto ack = ack_notifications();
    // 要么不含字段，要么是空数组
    if (ack.contains("resourceSubscriptions"))
    {
        EXPECT_TRUE(ack["resourceSubscriptions"].empty());
    }
}

TEST_F(SubscriptionsTest, MultipleResourceSubscriptionsPartiallyHonored)
{
    handlers->register_resource(ResourceDescriptor{
        .uri = "file:///known.txt",
        .name = "Known",
        .description = std::nullopt,
        .mime_type = "text/plain",
        .reader = []()
        { return json::array(); }});

    dispatch(make_listen_request(json{
        {"resourceSubscriptions",
         {"file:///known.txt", "file:///unknown.txt"}}}));

    auto ack = ack_notifications();
    ASSERT_TRUE(ack.contains("resourceSubscriptions"));
    ASSERT_EQ(ack["resourceSubscriptions"].size(), 1u);
    EXPECT_EQ(ack["resourceSubscriptions"][0], "file:///known.txt");
}

// ============================================================
// 错误路径
// ============================================================
TEST_F(SubscriptionsTest, RejectsMissingMeta)
{
    auto outcome = dispatch(make_listen_request(
        json{{"toolsListChanged", true}}, "sub-1",
        /*include_meta=*/false));

    EXPECT_EQ(outcome.kind, DispatchOutcome::Kind::Response);
    ASSERT_TRUE(outcome.payload.has_value());
    EXPECT_EQ((*outcome.payload)["error"]["code"], -32602);
    // 不应发送任何通知
    EXPECT_TRUE(writer->notifications.empty());
}

TEST_F(SubscriptionsTest, RejectsIncompleteMeta)
{
    json params = {
        {"notifications", {{"toolsListChanged", true}}},
        {"_meta", json::object()} // 缺失三个保留字段
    };
    auto outcome = dispatch(json{
        {"jsonrpc", "2.0"}, {"id", "sub-1"}, {"method", "subscriptions/listen"}, {"params", std::move(params)}});

    EXPECT_EQ(outcome.kind, DispatchOutcome::Kind::Response);
    EXPECT_EQ((*outcome.payload)["error"]["code"], -32602);
    EXPECT_TRUE(writer->notifications.empty());
}

// ============================================================
// 订阅注册表：writer 存活期间的行为
// ============================================================
TEST_F(SubscriptionsTest, TwoListensBothAcknowledged)
{
    dispatch(make_listen_request(
        json{{"toolsListChanged", true}}, "sub-1"));
    dispatch(make_listen_request(
        json{{"promptsListChanged", true}}, "sub-2"));

    ASSERT_EQ(writer->notifications.size(), 2u);
    EXPECT_EQ(writer->notifications[0]["params"]["_meta"]
                                   ["io.modelcontextprotocol/subscriptionId"],
              "\"sub-1\"");
    EXPECT_EQ(writer->notifications[1]["params"]["_meta"]
                                   ["io.modelcontextprotocol/subscriptionId"],
              "\"sub-2\"");
}

// ============================================================
// 通知 method 常量的正确性
// ============================================================
TEST_F(SubscriptionsTest, AcknowledgedMethodNameMatchesSpec)
{
    dispatch(make_listen_request(json{{"toolsListChanged", true}}));
    // 2026-07-28 使用下划线，不是驼峰
    EXPECT_EQ(first_notification()["method"],
              "notifications/subscriptions/acknowledged");
}

TEST_F(SubscriptionsTest, SubscriptionIdKeyMatchesSpec)
{
    dispatch(make_listen_request(json{{"toolsListChanged", true}}));
    const auto &meta = first_notification()["params"]["_meta"];
    EXPECT_TRUE(meta.contains(
        "io.modelcontextprotocol/subscriptionId"));
}