#include "NodeCreateHelper.h"
#include "ServerNode.h"

#include <iostream>
#include <string>

namespace
{

std::string ResolveErrorMessage(xs::node::NodeErrorCode code, std::string_view error_message)
{
    if (!error_message.empty())
    {
        return std::string(error_message);
    }

    return std::string(xs::node::NodeErrorMessage(code));
}

} // namespace

int main(int argc, char* argv[])
{
    xs::node::NodeCreateHelper create_helper;
    const xs::node::NodeErrorCode parse_result = create_helper.ParseCommandLine(argc, argv);
    if (parse_result != xs::node::NodeErrorCode::None)
    {
        std::cerr << ResolveErrorMessage(parse_result, create_helper.last_error_message()) << '\n';
        return 1;
    }

    xs::node::ServerNodePtr node;
    const xs::node::NodeErrorCode create_result = create_helper.CreateNode(&node);
    if (create_result != xs::node::NodeErrorCode::None)
    {
        std::cerr << ResolveErrorMessage(create_result, create_helper.last_error_message()) << '\n';
        return 1;
    }

    const xs::node::NodeErrorCode init_result = node->Init();
    if (init_result != xs::node::NodeErrorCode::None)
    {
        std::cerr << ResolveErrorMessage(init_result, node->last_error_message()) << '\n';
        return 1;
    }

    const xs::node::NodeErrorCode run_result = node->Run();
    const std::string run_error_message = ResolveErrorMessage(run_result, node->last_error_message());
    const xs::node::NodeErrorCode uninit_result = node->Uninit();

    if (run_result != xs::node::NodeErrorCode::None)
    {
        std::cerr << run_error_message << '\n';
        return 1;
    }

    if (uninit_result != xs::node::NodeErrorCode::None)
    {
        std::cerr << ResolveErrorMessage(uninit_result, node->last_error_message()) << '\n';
        return 1;
    }

    return 0;
}
