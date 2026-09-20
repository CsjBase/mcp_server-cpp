#include "test_helpers.h"
#include "mcp/json_rpc/DefaultRequestContext.h"

using namespace mcp;
using json = nlohmann::json;

class DefaultRequestContextTest : public ::testing::Test
{
protected:
    std::shared_ptr<test::TestMessageWriter> writer =
        std::make_shared<test::TestMessageWriter>();
    RequestMeta meta;

    DefaultRequestContext make_ctx(std::optional<ProgressToken> token)
    {
        return DefaultRequestContext(meta, std::move(token), writer);
    }
};

TEST_F(DefaultRequestContextTest, ProgressIsNoOpWithoutToken)
{
    auto ctx = make_ctx(std::nullopt);
    ctx.report_progress(0.5, 1.0, "half");
    EXPECT_TRUE(writer->notifications.empty());
}

TEST_F(DefaultRequestContextTest, ProgressEmitsCorrectNotification)
{
    auto ctx = make_ctx(ProgressToken(std::string{"tok-1"}));
    ctx.report_progress(0.25, 1.0, "quarter");

    ASSERT_EQ(writer->notifications.size(), 1u);
    const auto &n = writer->notifications[0];
    EXPECT_EQ(n["jsonrpc"], "2.0");
    EXPECT_EQ(n["method"], "notifications/progress");
    EXPECT_EQ(n["params"]["progressToken"], "tok-1");
    EXPECT_EQ(n["params"]["progress"], 0.25);
    EXPECT_EQ(n["params"]["total"], 1.0);
    EXPECT_EQ(n["params"]["message"], "quarter");
}

TEST_F(DefaultRequestContextTest, ProgressOmitsOptionalFields)
{
    auto ctx = make_ctx(ProgressToken(int64_t{9}));
    ctx.report_progress(0.5);

    const auto &params = writer->notifications[0]["params"];
    EXPECT_FALSE(params.contains("total"));
    EXPECT_FALSE(params.contains("message"));
    EXPECT_EQ(params["progressToken"], 9);
}

TEST_F(DefaultRequestContextTest, HasProgressTokenReflectsConstruction)
{
    auto with = make_ctx(ProgressToken(std::string{"x"}));
    auto without = make_ctx(std::nullopt);
    EXPECT_TRUE(with.has_progress_token());
    EXPECT_FALSE(without.has_progress_token());
}

TEST_F(DefaultRequestContextTest, MetaIsAccessible)
{
    meta.protocol_version = "2026-07-28";
    auto ctx = make_ctx(std::nullopt);
    EXPECT_EQ(ctx.meta().protocol_version, "2026-07-28");
}