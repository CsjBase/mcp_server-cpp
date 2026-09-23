#include "test_helpers.h"
#include "mcp/json_rpc/InputValidator.h"

using namespace mcp;
using json = nlohmann::json;

class InputValidatorTest : public ::testing::Test
{
protected:
    static json schema_string()
    {
        return json{
            {"type", "object"},
            {"properties", {{"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", 10}}}}},
            {"required", {"name"}}};
    }

    static json schema_full()
    {
        return json{
            {"type", "object"},
            {"properties", {{"id", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}, {"mode", {{"type", "string"}, {"enum", {"fast", "slow"}}}}, {"tags", {{"type", "array"}, {"items", {{"type", "string"}}}}}, {"nested", {{"type", "object"}, {"required", {"x"}}, {"properties", {{"x", {{"type", "boolean"}}}}}}}}},
            {"required", {"id", "mode"}}};
    }
};

TEST_F(InputValidatorTest, AcceptsValidSimpleInput)
{
    InputValidator v(schema_string());
    EXPECT_TRUE(v.validate(json{{"name", "hello"}}));
}

TEST_F(InputValidatorTest, RejectsMissingRequired)
{
    InputValidator v(schema_string());
    EXPECT_FALSE(v.validate(json::object()));
    ASSERT_FALSE(v.last_errors().empty());
    EXPECT_EQ(v.last_errors()[0]["path"], "name");
}

TEST_F(InputValidatorTest, RejectsWrongType)
{
    InputValidator v(schema_string());
    EXPECT_FALSE(v.validate(json{{"name", 123}}));
}

TEST_F(InputValidatorTest, RejectsTooShortString)
{
    InputValidator v(schema_string());
    EXPECT_FALSE(v.validate(json{{"name", ""}}));
}

TEST_F(InputValidatorTest, RejectsTooLongString)
{
    InputValidator v(schema_string());
    EXPECT_FALSE(v.validate(json{{"name", "01234567890"}}));
}

TEST_F(InputValidatorTest, RejectsOutOfRangeInteger)
{
    InputValidator v(schema_full());
    EXPECT_FALSE(v.validate(json{{"id", 0}, {"mode", "fast"}}));
    EXPECT_FALSE(v.validate(json{{"id", 101}, {"mode", "fast"}}));
}

TEST_F(InputValidatorTest, RejectsEnumViolation)
{
    InputValidator v(schema_full());
    EXPECT_FALSE(v.validate(json{{"id", 1}, {"mode", "medium"}}));
}

TEST_F(InputValidatorTest, ValidatesArrayItems)
{
    InputValidator v(schema_full());
    EXPECT_TRUE(v.validate(json{{"id", 1}, {"mode", "fast"}, {"tags", {"a", "b"}}}));
    EXPECT_FALSE(v.validate(json{{"id", 1}, {"mode", "fast"}, {"tags", {"a", 42}}}));
}

TEST_F(InputValidatorTest, ValidatesNestedObject)
{
    InputValidator v(schema_full());
    EXPECT_TRUE(v.validate(json{{"id", 1}, {"mode", "fast"}, {"nested", {{"x", true}}}}));
    EXPECT_FALSE(v.validate(json{{"id", 1}, {"mode", "fast"}, {"nested", json::object()}}));
}

TEST_F(InputValidatorTest, AccumulatesMultipleErrors)
{
    InputValidator v(schema_full());
    // 缺少 id、mode，且 nested 缺 x
    EXPECT_FALSE(v.validate(json{{"nested", json::object()}}));
    EXPECT_GE(v.last_errors().size(), 3u);
}

TEST_F(InputValidatorTest, ErrorsAreResetBetweenCalls)
{
    InputValidator v(schema_string());
    EXPECT_FALSE(v.validate(json::object()));
    EXPECT_FALSE(v.last_errors().empty());
    EXPECT_TRUE(v.validate(json{{"name", "ok"}}));
    EXPECT_TRUE(v.last_errors().empty());
}