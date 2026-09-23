#include <gtest/gtest.h>
#include "mcp/server/UriTemplate.h"

using namespace mcp;

// ============================================================
// 基本匹配
// ============================================================
TEST(UriTemplateTest, MatchesSingleVariable)
{
    UriTemplate t("file:///logs/{date}.log");
    auto p = t.match("file:///logs/2024-01-01.log");
    ASSERT_TRUE(p);
    EXPECT_EQ(p->at("date"), "2024-01-01");
}

TEST(UriTemplateTest, MatchesMultipleVariables)
{
    UriTemplate t("users/{user}/repos/{repo}");
    auto p = t.match("users/alice/repos/mcp");
    ASSERT_TRUE(p);
    EXPECT_EQ(p->at("user"), "alice");
    EXPECT_EQ(p->at("repo"), "mcp");
}

TEST(UriTemplateTest, MatchesVariableAtEnd)
{
    UriTemplate t("users/{id}");
    auto p = t.match("users/123");
    ASSERT_TRUE(p);
    EXPECT_EQ(p->at("id"), "123");
}

TEST(UriTemplateTest, MatchesVariableAtStart)
{
    UriTemplate t("{scheme}://host");
    auto p = t.match("https://host");
    ASSERT_TRUE(p);
    EXPECT_EQ(p->at("scheme"), "https");
}

TEST(UriTemplateTest, MatchesPureLiteralExactly)
{
    UriTemplate t("file:///static/config.json");
    EXPECT_TRUE(t.match("file:///static/config.json").has_value());
    EXPECT_FALSE(t.match("file:///static/other.json").has_value());
}

// ============================================================
// 非贪婪匹配
// ============================================================
TEST(UriTemplateTest, NonGreedyBetweenLiterals)
{
    UriTemplate t("{a}-{b}");
    auto p = t.match("x-y-z");
    ASSERT_TRUE(p);
    EXPECT_EQ(p->at("a"), "x");
    EXPECT_EQ(p->at("b"), "y-z");
}

TEST(UriTemplateTest, NonGreedyWithRepeatedSeparator)
{
    UriTemplate t("logs/{date}/{level}");
    auto p = t.match("logs/2024-01-01/info");
    ASSERT_TRUE(p);
    EXPECT_EQ(p->at("date"), "2024-01-01");
    EXPECT_EQ(p->at("level"), "info");
}

// ============================================================
// 普通变量 vs {+var}
// ============================================================
TEST(UriTemplateTest, RejectsSlashInPlainVariable)
{
    UriTemplate t("file:///logs/{date}.log");
    // 普通变量不得跨越路径分隔符
    EXPECT_FALSE(t.match("file:///logs/2024/01/01.log").has_value());
}

TEST(UriTemplateTest, AllowsSlashInReservedVariable)
{
    UriTemplate t("file://{+path}");
    auto p = t.match("file:///a/b/c.txt");
    ASSERT_TRUE(p);
    EXPECT_EQ(p->at("path"), "/a/b/c.txt");
}

TEST(UriTemplateTest, ReservedVariableAtStart)
{
    UriTemplate t("{+path}");
    auto p = t.match("/a/b/c");
    ASSERT_TRUE(p);
    EXPECT_EQ(p->at("path"), "/a/b/c");
}

// ============================================================
// 拒绝情形
// ============================================================
TEST(UriTemplateTest, RejectsTrailingContent)
{
    UriTemplate t("users/{id}");
    EXPECT_FALSE(t.match("users/123/extra").has_value());
}

TEST(UriTemplateTest, RejectsMissingLiteral)
{
    UriTemplate t("users/{id}/profile");
    EXPECT_FALSE(t.match("users/123/posts").has_value());
}

TEST(UriTemplateTest, RejectsEmptyVariableValue)
{
    UriTemplate t("users/{id}/profile");
    EXPECT_FALSE(t.match("users//profile").has_value());
}

TEST(UriTemplateTest, RejectsLiteralMismatch)
{
    UriTemplate t("file:///logs/{date}.log");
    EXPECT_FALSE(t.match("file:///other/2024-01-01.log").has_value());
}

TEST(UriTemplateTest, RejectsUnrelatedUri)
{
    UriTemplate t("users/{id}");
    EXPECT_FALSE(t.match("posts/42").has_value());
    EXPECT_FALSE(t.match("").has_value());
}

// ============================================================
// 解析期快速失败
// ============================================================
TEST(UriTemplateParseTest, ThrowsOnUnclosedBrace)
{
    EXPECT_THROW(UriTemplate("{unclosed"), std::invalid_argument);
}

TEST(UriTemplateParseTest, ThrowsOnEmptyExpression)
{
    EXPECT_THROW(UriTemplate("prefix/{}"), std::invalid_argument);
}

TEST(UriTemplateParseTest, ThrowsOnQueryExpansion)
{
    EXPECT_THROW(UriTemplate("{?query}"), std::invalid_argument);
}

TEST(UriTemplateParseTest, ThrowsOnFragmentExpansion)
{
    EXPECT_THROW(UriTemplate("{#frag}"), std::invalid_argument);
}

TEST(UriTemplateParseTest, ThrowsOnPathExpansion)
{
    EXPECT_THROW(UriTemplate("{/seg}"), std::invalid_argument);
}

TEST(UriTemplateParseTest, ThrowsOnMultiVariableExpansion)
{
    EXPECT_THROW(UriTemplate("{a,b}"), std::invalid_argument);
}

// ============================================================
// 元信息
// ============================================================
TEST(UriTemplateMetaTest, ReportsHasVariables)
{
    EXPECT_TRUE(UriTemplate("users/{id}").has_variables());
    EXPECT_FALSE(UriTemplate("static/path").has_variables());
}

TEST(UriTemplateMetaTest, PreservesOriginal)
{
    const std::string tmpl = "file:///logs/{date}.log";
    EXPECT_EQ(UriTemplate(tmpl).original(), tmpl);
}

// ============================================================
// 参数提取不保留无关字段
// ============================================================
TEST(UriTemplateTest, ExtractedParamsContainOnlyDeclaredVariables)
{
    UriTemplate t("{a}/{b}");
    auto p = t.match("x/y");
    ASSERT_TRUE(p);
    EXPECT_EQ(p->size(), 2u);
    EXPECT_TRUE(p->count("a"));
    EXPECT_TRUE(p->count("b"));
}