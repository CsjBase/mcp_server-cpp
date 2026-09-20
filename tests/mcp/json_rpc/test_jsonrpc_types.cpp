#include "test_helpers.h"
#include "mcp/json_rpc/JsonRpcTypes.h"

using namespace mcp;
using json = nlohmann::json;

// ============================================================
// RequestId：只接受 string 或 integer，拒绝 null/float/object/array
// ============================================================
TEST(RequestIdTest, AcceptsString)
{
    auto id = RequestId::from_json(json("abc"));
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(id->to_json(), json("abc"));
}

TEST(RequestIdTest, AcceptsInteger)
{
    auto id = RequestId::from_json(json(42));
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(id->to_json(), json(42));
}

TEST(RequestIdTest, RejectsNull)
{
    EXPECT_FALSE(RequestId::from_json(json(nullptr)).has_value());
}

TEST(RequestIdTest, RejectsFloat)
{
    // JSON-RPC 2.0 允许数字，但 MCP 2026-07-28 明确要求 integer
    EXPECT_FALSE(RequestId::from_json(json(3.14)).has_value());
}

TEST(RequestIdTest, RejectsObjectAndArray)
{
    EXPECT_FALSE(RequestId::from_json(json::object()).has_value());
    EXPECT_FALSE(RequestId::from_json(json::array()).has_value());
}

TEST(RequestIdTest, RoundTripPreservesType)
{
    auto s = RequestId::from_json(json("x"));
    auto i = RequestId::from_json(json(7));
    ASSERT_TRUE(s && i);
    EXPECT_EQ(s->to_json().type(), json::value_t::string);
    EXPECT_EQ(i->to_json().type(), json::value_t::number_integer);
}

// ============================================================
// Request：必须含 jsonrpc=2.0、method、合法 id
// ============================================================
TEST(RequestTest, ParsesValidRequest)
{
    json j = test::make_request("tools/list", {{"_meta", test::valid_meta()}});
    auto req = Request::from_json(j);
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->method, "tools/list");
    EXPECT_EQ(req->id.to_json(), json(1));
}

TEST(RequestTest, RejectsWrongJsonrpcVersion)
{
    json j = test::make_request("x", json::object());
    j["jsonrpc"] = "1.0";
    EXPECT_FALSE(Request::from_json(j).has_value());
}

TEST(RequestTest, RejectsMissingMethod)
{
    json j = {{"jsonrpc", "2.0"}, {"id", 1}};
    EXPECT_FALSE(Request::from_json(j).has_value());
}

TEST(RequestTest, RejectsMissingId)
{
    json j = {{"jsonrpc", "2.0"}, {"method", "x"}};
    EXPECT_FALSE(Request::from_json(j).has_value());
}

TEST(RequestTest, RejectsNullId)
{
    json j = test::make_request("x", json::object());
    j["id"] = nullptr;
    EXPECT_FALSE(Request::from_json(j).has_value());
}

TEST(RequestTest, ExtractMetaReturnsValidMeta)
{
    json j = test::make_request("x", {{"_meta", test::valid_meta()}});
    auto req = Request::from_json(j);
    ASSERT_TRUE(req);
    auto meta = req->extract_meta();
    ASSERT_TRUE(meta);
    EXPECT_TRUE(meta->is_valid());
    EXPECT_EQ(meta->protocol_version, "2026-07-28");
}

TEST(RequestTest, ExtractMetaReturnsNulloptWhenMissing)
{
    json j = test::make_request("x", json::object());
    auto req = Request::from_json(j);
    ASSERT_TRUE(req);
    EXPECT_FALSE(req->extract_meta().has_value());
}

// ============================================================
// Notification：不得包含 id
// ============================================================
TEST(NotificationTest, ParsesValidNotification)
{
    json j = {{"jsonrpc", "2.0"}, {"method", "notifications/x"}};
    auto n = Notification::from_json(j);
    ASSERT_TRUE(n);
    EXPECT_EQ(n->method, "notifications/x");
}

TEST(NotificationTest, RejectsNotificationWithId)
{
    json j = {{"jsonrpc", "2.0"}, {"method", "x"}, {"id", 1}};
    EXPECT_FALSE(Notification::from_json(j).has_value());
}

TEST(NotificationTest, ToJsonNeverContainsId)
{
    Notification n{"notifications/x", json::object()};
    EXPECT_FALSE(n.to_json().contains("id"));
}

// ============================================================
// SuccessResponse：make() 必须自动注入 resultType
// ============================================================
TEST(SuccessResponseTest, MakeInjectsResultType)
{
    auto resp = SuccessResponse::make(RequestId(int64_t{1}),
                                      json{{"tools", json::array()}});
    EXPECT_EQ(resp.result["resultType"], "complete");
}

TEST(SuccessResponseTest, MakePreservesExplicitResultType)
{
    auto resp = SuccessResponse::make(
        RequestId(int64_t{1}),
        json{{"resultType", "input_required"}});
    EXPECT_EQ(resp.result["resultType"], "input_required");
}

TEST(SuccessResponseTest, RejectsResponseWithBothResultAndError)
{
    json j = {
        {"jsonrpc", "2.0"}, {"id", 1}, {"result", json::object()}, {"error", {{"code", -32603}, {"message", "x"}}}};
    EXPECT_FALSE(SuccessResponse::from_json(j).has_value());
}

// ============================================================
// ErrorResponse：请求无法解析时 id 可以省略
// ============================================================
TEST(ErrorResponseTest, ToJsonOmitsIdWhenNullopt)
{
    auto err = make_error(ErrorCode::ParseError);
    json j = err.to_json();
    EXPECT_TRUE(j["id"].is_null());
}

TEST(ErrorResponseTest, ToJsonIncludesIdWhenPresent)
{
    auto err = make_error(ErrorCode::InvalidParams, RequestId(int64_t{5}));
    EXPECT_EQ(err.to_json()["id"], 5);
}

TEST(ErrorResponseTest, FromJsonParsesStandardError)
{
    json j = {
        {"jsonrpc", "2.0"}, {"id", 1}, {"error", {{"code", -32602}, {"message", "bad params"}}}};
    auto err = ErrorResponse::from_json(j);
    ASSERT_TRUE(err);
    EXPECT_EQ(err->error.code, -32602);
    EXPECT_EQ(err->error.message, "bad params");
}

// ============================================================
// RequestMeta：三个保留字段必须齐备
// ============================================================
TEST(RequestMetaTest, ValidWhenAllReservedFieldsPresent)
{
    auto meta = RequestMeta::from_json(test::valid_meta());
    ASSERT_TRUE(meta);
    EXPECT_TRUE(meta->is_valid());
}

TEST(RequestMetaTest, InvalidWhenClientInfoMissing)
{
    json j = test::valid_meta();
    j.erase("io.modelcontextprotocol/clientInfo");
    auto meta = RequestMeta::from_json(j);
    ASSERT_TRUE(meta);
    EXPECT_FALSE(meta->is_valid());
}

TEST(RequestMetaTest, InvalidWhenClientCapabilitiesEmpty)
{
    json j = test::valid_meta();
    j["io.modelcontextprotocol/clientCapabilities"] = json::object();
    auto meta = RequestMeta::from_json(j);
    ASSERT_TRUE(meta);
    EXPECT_FALSE(meta->is_valid());
}