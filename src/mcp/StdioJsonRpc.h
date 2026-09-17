#pragma once

#include "JsonRpc.h"

namespace mcp
{
    class StdioJsonRpc
    {
    public:
        explicit StdioJsonRpc(JsonRpcMethodDispatcher::ptr dispatcher)
            : rpc_(std::move(dispatcher)) {}

        StdioJsonRpc(JsonRpcMethodDispatcher::ptr dispatcher, std::istream &in, std::ostream &out)
            : rpc_(std::move(dispatcher)), in_(in), out_(out) {}

        void run();

    private:
        JsonRpc rpc_;
        std::istream &in_ = std::cin;
        std::ostream &out_ = std::cout;
    };
}