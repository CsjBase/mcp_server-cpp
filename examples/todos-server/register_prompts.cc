#include "mcp/McpServer.h"
#include "TodoStore.h"

namespace todos
{

    using json = nlohmann::json;
    using mcp::PromptDescriptor;

    void register_prompts(mcp::McpServer &server,
                          std::shared_ptr<TodoStore> store)
    {
        server.register_prompt(PromptDescriptor{
            .name = "daily_review",
            .description = "Review today's todos and plan the day",
            .arguments = {
                {"focus", "Optional area to focus on", false}},
            .renderer = [store](const json &args) -> json
            {
                std::string focus = args.value("focus", "general productivity");

                // 在 renderer 内部读取当前待办状态，
                // 把它作为上下文注入 prompt。
                // 这是资源与 prompt 的协同：prompt 内容动态反映
                // 当前应用状态。
                json pending = json::array();
                json done = json::array();
                for (const auto &t : store->list())
                {
                    (t.completed ? done : pending).push_back(t.title);
                }

                std::string assistant_text =
                    "You are a productivity assistant. Help the user "
                    "review their todos with a focus on " +
                    focus + ".";

                std::string user_text =
                    "Pending todos:\n";
                if (pending.empty())
                {
                    user_text += "  (none)\n";
                }
                else
                {
                    for (const auto &t : pending)
                        user_text += "  - " + t.get<std::string>() + "\n";
                }

                user_text += "\nCompleted:\n";
                if (done.empty())
                {
                    user_text += "  (none)\n";
                }
                else
                {
                    for (const auto &t : done)
                        user_text += "  - " + t.get<std::string>() + "\n";
                }

                user_text += "\nPlease suggest what to prioritize next.";

                return json::array({{{"role", "assistant"},
                                     {"content", {{"type", "text"}, {"text", assistant_text}}}},
                                    {{"role", "user"},
                                     {"content", {{"type", "text"}, {"text", user_text}}}}});
            }});
    }

} // namespace todos