#include "mcp/StdioJsonRpc.h"

using json = nlohmann::json;

int main()
{
    auto dispatcher = std::make_unique<mcp::JsonRpcMethodDispatcher>();
    dispatcher->registerHandler("echo", mcp::JsonRpcMethodDispatcher::MethodEntry{[](const json &params) {},
                                                                                  [](const json &params) -> json
                                                                                  { return params; }});
    auto calculate_validator = [](const json &params)
    {
        if (!params.contains("operation"))
            throw mcp::InvalidParams{"Missing operation"};
        if (!params.contains("a") || !params.contains("b"))
            throw mcp::InvalidParams{"Missing a or b"};
        if (!params["a"].is_number() || !params["b"].is_number())
        {
            throw mcp::InvalidParams{"Invalid type a or b"};
        }

        const std::string op = params.at("operation").get<std::string>();
        if (op != "+" && op != "-" && op != "*" && op != "/")
            throw mcp::InvalidParams{"Invalid operation"};

        const double b = params.at("b").get<double>();
        if (op == "/" && b == 0)
            throw mcp::InvalidParams{"Division by zero"};
    };

    auto calculate_callback = [](const json &params) -> json
    {
        const std::string op = params.at("operation").get<std::string>();
        const double a = params.at("a").get<double>();
        const double b = params.at("b").get<double>();

        double result = 0;
        if (op == "+")
            result = a + b;
        else if (op == "-")
            result = a - b;
        else if (op == "*")
            result = a * b;
        else if (op == "/")
            result = a / b;

        return json{{"result", result}};
    };

    dispatcher->registerHandler("calculate", {calculate_validator, calculate_callback});
    mcp::StdioJsonRpc stdio_json_rpc(std::move(dispatcher));
    stdio_json_rpc.run();
    // {"jsonrpc":"2.0","method":"calculate","id":10,"params":{"operation":"+","a":10,"b":20}}
    // {"jsonrpc":"2.0","method":"calculate","id":10,"params":{"operation":"/","a":10,"b":0}}
    // {"jsonrpc":"2.0","method":"calculate","id":10,"params":{"operation":"/","a":"10","b":20}}
    // {"jsonrpc":"2.0","method":"calculate","id":10,"params":{"operation":"**","a":10,"b":20}}
    // {"jsonrpc":"2.0","method":"calculate","id":10,"params":{"operation":"+","a":10.2,"b":20.3}}
    // {"jsonrpc"："2.0","method":"calculate","id":10,"params":{"operation":"+","a":10.2,"b":20.3}}
    // {"jsonrpc":"1.0","method":"calculate","id":10,"params":{"operation":"+","a":10.2,"b":20.3}}
    // {"method":"calculate","id":10,"params":{"operation":"+","a":10.2,"b":20.3}}
    // {"jsonrpc":"2.0","id":10,"params":{"operation":"+","a":10.2,"b":20.3}}
    // {"jsonrpc":"1.0","id":10,"method":"calculate","params":{"operation":"+","a":10.2,"b":20.3}}
    // {"jsonrpc":"2.0","method":"calculate","params":{"operation":"+","a":10.2,"b":20.3}}
    // []
    // [{"jsonrpc":"2.0","method":"calculate","id":10,"params":{"operation":"+","a":10,"b":20}}, {"jsonrpc":"2.0","method":"calculate","id":10,"params":{"operation":"+","a":15,"b":20}}]
    // [{"jsonrpc":"2.0","method":"calculate","id":10,"params":{"operation":"+","a":10,"b":20}}, {"jsonrpc":"2.0","id":10,"params":{"operation":"+","a":10.2,"b":20.3}}]
    // [{"jsonrpc":"2.0","id":10,"params":{"operation":"+","a":10.2,"b":20.3}}, {"jsonrpc":"1.0","method":"calculate","params":{"operation":"+","a":10.2,"b":20.3}}]
    // [{"jsonrpc":"2.0","method":"calculate","params":{"operation":"+","a":10.2,"b":20.3}},{"jsonrpc":"2.0","method":"calculate","params":{"operation":"+","a":10.2,"b":20.3}}]
    //
    // {"jsonrpc":"2.0","id":10,"method":"echo","params":"aaaaa"}
    // {"jsonrpc":"2.0","id":10,"method":"echo"}
    // {"jsonrpc":"2.0","id":10,"method":"echo","params":null}
    // {"jsonrpc":"2.0","id":10,"method":"echo","params":10.23}
}