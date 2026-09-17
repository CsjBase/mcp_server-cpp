#include "StdioJsonRpc.h"
#include "Log.h"

namespace mcp
{
    void StdioJsonRpc::run()
    {
        MCP_LOG_INFO("JSON-RPC stdio server starting...");

        std::string body;

        while (true)
        {
            std::getline(in_, body);
            if (in_.eof())
            {
                MCP_LOG_INFO("stdin EOF reached, exiting");
                break;
            }
            auto result = rpc_.dispatch(body);
            if (result)
            {
                out_ << *result << std::endl;
            }
        }
    }
}