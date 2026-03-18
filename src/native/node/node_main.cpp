#include "NodeRuntime.h"

#include <iostream>
#include <string>
#include <utility>

namespace
{

std::string ResolveErrorMessage(xs::node::NodeRuntimeErrorCode code, std::string error_message)
{
    if (!error_message.empty())
    {
        return error_message;
    }

    return std::string(xs::node::NodeRuntimeErrorMessage(code));
}

} // namespace

int main(int argc, char* argv[])
{
    xs::node::NodeCommandLineArgs args;
    std::string error_message;
    const xs::node::NodeRuntimeErrorCode parse_result =
        xs::node::ParseNodeCommandLine(argc, argv, &args, &error_message);
    if (parse_result != xs::node::NodeRuntimeErrorCode::None)
    {
        std::cerr << ResolveErrorMessage(parse_result, std::move(error_message)) << '\n';
        return 1;
    }

    error_message.clear();
    const xs::node::NodeRuntimeErrorCode run_result =
        xs::node::RunNodeProcess(args, xs::node::NodeRuntimeRunOptions{}, &error_message);
    if (run_result != xs::node::NodeRuntimeErrorCode::None)
    {
        std::cerr << ResolveErrorMessage(run_result, std::move(error_message)) << '\n';
        return 1;
    }

    return 0;
}