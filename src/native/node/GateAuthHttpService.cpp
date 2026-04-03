#include "GateAuthHttpService.h"

#include "Json.h"

#include <asio/error.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xs::node
{
namespace
{

using Tcp = asio::ip::tcp;

constexpr std::size_t kMaxHeaderBytes = 8192u;
constexpr std::size_t kMaxBodyBytes = 65536u;

struct HttpResponse final
{
    std::uint16_t status_code{200};
    std::string content_type{"application/json; charset=utf-8"};
    std::string body{};
};

struct ParsedHttpRequest final
{
    std::string method{};
    std::string path{};
    std::string body{};
};

enum class RequestParseState : std::uint8_t
{
    Incomplete = 0,
    Complete,
    Invalid,
};

struct RequestParseResult final
{
    RequestParseState state{RequestParseState::Incomplete};
    ParsedHttpRequest request{};
    std::string error_message{};
};

void ClearError(std::string& error_message) noexcept
{
    error_message.clear();
}

NodeErrorCode SetError(
    std::string& error_message,
    NodeErrorCode code,
    std::string message)
{
    if (message.empty())
    {
        error_message = std::string(NodeErrorMessage(code));
    }
    else
    {
        error_message = std::move(message);
    }

    return code;
}

std::string TrimAscii(std::string_view value)
{
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0)
    {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
    {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

std::string ToLowerAscii(std::string_view value)
{
    std::string text(value);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string EndpointToString(const xs::core::EndpointConfig& endpoint)
{
    std::ostringstream stream;
    stream << endpoint.host << ':' << endpoint.port;
    return stream.str();
}

std::string EndpointToString(const Tcp::endpoint& endpoint)
{
    std::ostringstream stream;
    if (endpoint.address().is_v6())
    {
        stream << '[' << endpoint.address().to_string() << ']';
    }
    else
    {
        stream << endpoint.address().to_string();
    }
    stream << ':' << endpoint.port();
    return stream.str();
}

std::optional<Tcp::endpoint> ResolveEndpoint(
    const xs::core::EndpointConfig& config,
    std::string* error_message)
{
    if (config.host.empty())
    {
        if (error_message != nullptr)
        {
            *error_message = "Gate auth HTTP listen endpoint host must not be empty.";
        }
        return std::nullopt;
    }

    if (config.port == 0u)
    {
        if (error_message != nullptr)
        {
            *error_message = "Gate auth HTTP listen endpoint port must be greater than zero.";
        }
        return std::nullopt;
    }

    asio::ip::address address;
    if (config.host == "localhost")
    {
        address = asio::ip::address_v4::loopback();
    }
    else
    {
        std::error_code error_code;
        address = asio::ip::make_address(config.host, error_code);
        if (error_code)
        {
            if (error_message != nullptr)
            {
                *error_message = "Gate auth HTTP listen endpoint host must be an IP literal or localhost.";
            }
            return std::nullopt;
        }
    }

    if (error_message != nullptr)
    {
        error_message->clear();
    }
    return Tcp::endpoint(address, config.port);
}

std::string_view HttpStatusText(std::uint16_t status_code) noexcept
{
    switch (status_code)
    {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Payload Too Large";
    case 500:
        return "Internal Server Error";
    case 503:
        return "Service Unavailable";
    }

    return "Unknown";
}

std::string BuildHttpResponseBuffer(const HttpResponse& response)
{
    std::ostringstream stream;
    stream << "HTTP/1.1 " << response.status_code << ' ' << HttpStatusText(response.status_code) << "\r\n";
    stream << "Content-Type: " << response.content_type << "\r\n";
    stream << "Content-Length: " << response.body.size() << "\r\n";
    stream << "Connection: close\r\n";
    stream << "\r\n";
    stream << response.body;
    return stream.str();
}

RequestParseResult TryParseRequest(std::string_view buffer)
{
    RequestParseResult result;

    const std::size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string_view::npos)
    {
        if (buffer.size() > kMaxHeaderBytes)
        {
            result.state = RequestParseState::Invalid;
            result.error_message = "HTTP request header exceeded the maximum supported size.";
        }
        return result;
    }

    const std::string header_text(buffer.substr(0, header_end));
    std::size_t line_start = 0;
    std::size_t line_end = header_text.find("\r\n");
    if (line_end == std::string::npos)
    {
        result.state = RequestParseState::Invalid;
        result.error_message = "HTTP request line is incomplete.";
        return result;
    }

    const std::string request_line = header_text.substr(line_start, line_end - line_start);
    std::istringstream request_stream(request_line);
    std::string method;
    std::string target;
    std::string version;
    if (!(request_stream >> method >> target >> version) || !request_stream.eof())
    {
        result.state = RequestParseState::Invalid;
        result.error_message = "HTTP request line is invalid.";
        return result;
    }

    if (version != "HTTP/1.1" && version != "HTTP/1.0")
    {
        result.state = RequestParseState::Invalid;
        result.error_message = "HTTP version is not supported.";
        return result;
    }

    std::size_t content_length = 0u;
    line_start = line_end + 2u;
    while (line_start < header_text.size())
    {
        line_end = header_text.find("\r\n", line_start);
        const std::string line =
            line_end == std::string::npos
                ? header_text.substr(line_start)
                : header_text.substr(line_start, line_end - line_start);
        if (!line.empty())
        {
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos)
            {
                result.state = RequestParseState::Invalid;
                result.error_message = "HTTP header line is invalid.";
                return result;
            }

            const std::string name = ToLowerAscii(TrimAscii(std::string_view(line).substr(0, colon)));
            const std::string value = TrimAscii(std::string_view(line).substr(colon + 1u));
            if (name == "content-length")
            {
                std::uint64_t parsed_length = 0u;
                const char* begin = value.data();
                const char* end = value.data() + value.size();
                const std::from_chars_result parse_result = std::from_chars(begin, end, parsed_length);
                if (parse_result.ec != std::errc{} || parse_result.ptr != end || parsed_length > kMaxBodyBytes)
                {
                    result.state = RequestParseState::Invalid;
                    result.error_message = "HTTP Content-Length is invalid.";
                    return result;
                }

                content_length = static_cast<std::size_t>(parsed_length);
            }
        }

        if (line_end == std::string::npos)
        {
            break;
        }
        line_start = line_end + 2u;
    }

    const std::size_t body_offset = header_end + 4u;
    if (buffer.size() < body_offset + content_length)
    {
        return result;
    }

    result.state = RequestParseState::Complete;
    result.request.method = std::move(method);
    result.request.path = std::move(target);
    result.request.body = std::string(buffer.substr(body_offset, content_length));
    return result;
}

HttpResponse BuildJsonErrorResponse(std::uint16_t status_code, std::string message)
{
    xs::core::Json body{
        {"error", std::move(message)},
    };

    HttpResponse response;
    response.status_code = status_code;
    response.body = body.dump();
    return response;
}

bool TryReadNonEmptyString(
    const xs::core::Json& object,
    std::string_view key,
    std::string* output,
    std::string* error_message)
{
    if (output == nullptr)
    {
        if (error_message != nullptr)
        {
            *error_message = "JSON string output must not be null.";
        }
        return false;
    }

    const auto iterator = object.find(std::string(key));
    if (iterator == object.end())
    {
        if (error_message != nullptr)
        {
            *error_message = "Missing required field '" + std::string(key) + "'.";
        }
        return false;
    }

    if (!iterator->is_string())
    {
        if (error_message != nullptr)
        {
            *error_message = "Field '" + std::string(key) + "' must be a string.";
        }
        return false;
    }

    *output = iterator->get<std::string>();
    if (output->empty())
    {
        if (error_message != nullptr)
        {
            *error_message = "Field '" + std::string(key) + "' must not be empty.";
        }
        return false;
    }

    if (error_message != nullptr)
    {
        error_message->clear();
    }
    return true;
}

} // namespace

class GateAuthHttpService::Impl final
{
    class Connection final : public std::enable_shared_from_this<Connection>
    {
      public:
        Connection(Impl& owner, Tcp::socket socket)
            : owner_(owner),
              socket_(std::move(socket))
        {
            std::error_code error_code;
            const Tcp::endpoint remote_endpoint = socket_.remote_endpoint(error_code);
            if (!error_code)
            {
                remote_address_ = remote_endpoint.address().to_string();
            }
        }

        void Start()
        {
            DoRead();
        }

        void ForceClose()
        {
            if (closed_)
            {
                return;
            }

            closed_ = true;
            std::error_code error_code;
            socket_.shutdown(Tcp::socket::shutdown_both, error_code);
            socket_.close(error_code);
        }

      private:
        void DoRead()
        {
            auto self = shared_from_this();
            socket_.async_read_some(asio::buffer(read_buffer_), [self](const std::error_code& error_code, std::size_t bytes_read) {
                self->HandleRead(error_code, bytes_read);
            });
        }

        void HandleRead(const std::error_code& error_code, std::size_t bytes_read)
        {
            if (closed_)
            {
                return;
            }

            if (error_code)
            {
                CloseAndRemove();
                return;
            }

            request_buffer_.append(read_buffer_.data(), bytes_read);
            if (request_buffer_.size() > kMaxHeaderBytes + kMaxBodyBytes)
            {
                WriteResponse(BuildJsonErrorResponse(413, "HTTP request exceeded the supported size."));
                return;
            }

            const RequestParseResult parse_result = TryParseRequest(request_buffer_);
            switch (parse_result.state)
            {
            case RequestParseState::Incomplete:
                DoRead();
                return;

            case RequestParseState::Invalid:
                WriteResponse(BuildJsonErrorResponse(400, parse_result.error_message));
                return;

            case RequestParseState::Complete:
                WriteResponse(owner_.HandleRequest(
                    parse_result.request.method,
                    parse_result.request.path,
                    parse_result.request.body,
                    remote_address_));
                return;
            }
        }

        void WriteResponse(HttpResponse response)
        {
            if (closed_)
            {
                return;
            }

            response_buffer_ = BuildHttpResponseBuffer(response);

            auto self = shared_from_this();
            asio::async_write(socket_, asio::buffer(response_buffer_), [self](const std::error_code& error_code, std::size_t) {
                self->HandleWrite(error_code);
            });
        }

        void HandleWrite(const std::error_code& error_code)
        {
            (void)error_code;
            CloseAndRemove();
        }

        void CloseAndRemove()
        {
            if (closed_)
            {
                return;
            }

            closed_ = true;
            std::error_code error_code;
            socket_.shutdown(Tcp::socket::shutdown_both, error_code);
            socket_.close(error_code);
            owner_.RemoveConnection(shared_from_this());
        }

        Impl& owner_;
        Tcp::socket socket_;
        std::array<char, 2048> read_buffer_{};
        std::string request_buffer_{};
        std::string response_buffer_{};
        std::string remote_address_{};
        bool closed_{false};
    };

  public:
    Impl(xs::core::MainEventLoop& event_loop, xs::core::Logger& logger, GateAuthHttpServiceOptions options)
        : event_loop_(event_loop),
          logger_(logger),
          options_(std::move(options)),
          configured_endpoint_text_(EndpointToString(options_.listen_endpoint))
    {
    }

    [[nodiscard]] NodeErrorCode Init()
    {
        if (initialized_)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "Gate auth HTTP service is already initialized.");
        }

        if (options_.node_id.empty())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "Gate auth HTTP service node_id must not be empty.");
        }

        if (!options_.login_handler)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "Gate auth HTTP login handler must not be empty.");
        }

        std::string endpoint_error;
        if (!ResolveEndpoint(options_.listen_endpoint, &endpoint_error).has_value())
        {
            return SetError(last_error_message_, NodeErrorCode::InvalidArgument, std::move(endpoint_error));
        }

        initialized_ = true;
        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode Run()
    {
        if (!initialized_)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "Gate auth HTTP service must be initialized before Run().");
        }

        if (running_)
        {
            ClearError(last_error_message_);
            return NodeErrorCode::None;
        }

        std::string endpoint_error;
        const std::optional<Tcp::endpoint> endpoint = ResolveEndpoint(options_.listen_endpoint, &endpoint_error);
        if (!endpoint.has_value())
        {
            return SetError(last_error_message_, NodeErrorCode::InvalidArgument, std::move(endpoint_error));
        }

        try
        {
            acceptor_ = std::make_unique<Tcp::acceptor>(event_loop_.context());
            acceptor_->open(endpoint->protocol());
            acceptor_->set_option(Tcp::acceptor::reuse_address(true));
            acceptor_->bind(*endpoint);
            acceptor_->listen();
            bound_endpoint_ = EndpointToString(acceptor_->local_endpoint());
        }
        catch (const std::exception& exception)
        {
            acceptor_.reset();
            bound_endpoint_.clear();
            return SetError(
                last_error_message_,
                NodeErrorCode::NodeRunFailed,
                std::string("Failed to start Gate auth HTTP listener: ") + exception.what());
        }

        running_ = true;
        StartAccept();

        const std::array<xs::core::LogContextField, 2> context{
            xs::core::LogContextField{"configuredEndpoint", EndpointToString(options_.listen_endpoint)},
            xs::core::LogContextField{"boundEndpoint", bound_endpoint_},
        };
        logger_.Log(xs::core::LogLevel::Info, "auth.http", "Gate auth HTTP listener started.", context);

        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode Uninit()
    {
        if (acceptor_ != nullptr)
        {
            std::error_code error_code;
            acceptor_->close(error_code);
            acceptor_.reset();
        }

        std::vector<std::shared_ptr<Connection>> active_connections;
        active_connections.reserve(connections_.size());
        for (const std::shared_ptr<Connection>& connection : connections_)
        {
            active_connections.push_back(connection);
        }
        connections_.clear();

        for (const std::shared_ptr<Connection>& connection : active_connections)
        {
            if (connection != nullptr)
            {
                connection->ForceClose();
            }
        }

        running_ = false;
        initialized_ = false;
        bound_endpoint_.clear();
        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] bool initialized() const noexcept
    {
        return initialized_;
    }

    [[nodiscard]] bool running() const noexcept
    {
        return running_;
    }

    [[nodiscard]] std::string_view configured_endpoint() const noexcept
    {
        return configured_endpoint_text_;
    }

    [[nodiscard]] std::string_view bound_endpoint() const noexcept
    {
        return bound_endpoint_;
    }

    [[nodiscard]] std::string_view last_error_message() const noexcept
    {
        return last_error_message_;
    }

  private:
    void StartAccept()
    {
        if (!running_ || acceptor_ == nullptr)
        {
            return;
        }

        acceptor_->async_accept([this](const std::error_code& error_code, Tcp::socket socket) mutable {
            HandleAccept(error_code, std::move(socket));
        });
    }

    void HandleAccept(const std::error_code& error_code, Tcp::socket socket)
    {
        if (!running_)
        {
            return;
        }

        if (!error_code)
        {
            auto connection = std::make_shared<Connection>(*this, std::move(socket));
            connections_.insert(connection);
            connection->Start();
        }
        else if (error_code != asio::error::operation_aborted)
        {
            const std::array<xs::core::LogContextField, 1> context{
                xs::core::LogContextField{"error", error_code.message()},
            };
            logger_.Log(xs::core::LogLevel::Warn, "auth.http", "Gate auth HTTP accept failed.", context);
        }

        StartAccept();
    }

    HttpResponse HandleRequest(
        std::string_view method,
        std::string_view path,
        std::string_view body,
        std::string_view remote_address)
    {
        HttpResponse response;
        try
        {
            response = RouteRequest(method, path, body, remote_address);
        }
        catch (const std::exception& exception)
        {
            response = BuildJsonErrorResponse(500, std::string("Unhandled gate auth handler exception: ") + exception.what());
        }
        catch (...)
        {
            response = BuildJsonErrorResponse(500, "Unhandled gate auth handler exception.");
        }

        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"method", std::string(method)},
            xs::core::LogContextField{"path", std::string(path)},
            xs::core::LogContextField{"remoteAddress", std::string(remote_address)},
            xs::core::LogContextField{"statusCode", std::to_string(response.status_code)},
        };
        logger_.Log(xs::core::LogLevel::Info, "auth.http", "Gate auth HTTP request handled.", context);
        return response;
    }

    HttpResponse RouteRequest(
        std::string_view method,
        std::string_view path,
        std::string_view body,
        std::string_view remote_address)
    {
        if (path == "/healthz")
        {
            if (method != "GET")
            {
                return BuildJsonErrorResponse(405, "Only GET is allowed for /healthz.");
            }

            xs::core::Json payload{
                {"status", "ok"},
                {"nodeId", options_.node_id},
            };

            HttpResponse response;
            response.status_code = 200;
            response.body = payload.dump();
            return response;
        }

        if (path == "/login")
        {
            if (method != "POST")
            {
                return BuildJsonErrorResponse(405, "Only POST is allowed for /login.");
            }

            xs::core::Json request_json;
            std::string json_error;
            if (xs::core::TryParseJson(body, &request_json, &json_error) != xs::core::JsonErrorCode::None)
            {
                return BuildJsonErrorResponse(400, std::string("Invalid login JSON: ") + json_error);
            }

            if (!request_json.is_object())
            {
                return BuildJsonErrorResponse(400, "Login request body must be a JSON object.");
            }

            GateAuthLoginRequest request;
            std::string validation_error;
            if (!TryReadNonEmptyString(request_json, "account", &request.account, &validation_error) ||
                !TryReadNonEmptyString(request_json, "password", &request.password, &validation_error))
            {
                return BuildJsonErrorResponse(400, validation_error);
            }
            request.remote_address = std::string(remote_address);

            const GateAuthLoginResult result = options_.login_handler(request);
            if (!result.success)
            {
                const std::uint16_t status_code = result.status_code == 0U ? 401U : result.status_code;
                return BuildJsonErrorResponse(
                    status_code,
                    result.error.empty() ? "Gate authentication failed." : result.error);
            }

            if (result.kcp_host.empty() || result.kcp_port == 0U || result.conversation == 0U)
            {
                return BuildJsonErrorResponse(500, "Gate authentication handler returned an incomplete KCP session payload.");
            }

            xs::core::Json payload{
                {"status", "ok"},
                {"nodeId", options_.node_id},
                {"gateNodeId", result.gate_node_id.empty() ? options_.node_id : result.gate_node_id},
                {"account", request.account},
                {"kcp",
                 xs::core::Json{
                     {"host", result.kcp_host},
                     {"port", result.kcp_port},
                     {"conversation", result.conversation},
                 }},
                {"issuedAtUnixMs", result.issued_at_unix_ms},
                {"expiresAtUnixMs", result.expires_at_unix_ms},
            };

            HttpResponse response;
            response.status_code = 200;
            response.body = payload.dump();
            return response;
        }

        return BuildJsonErrorResponse(404, "Gate auth route was not found.");
    }

    void RemoveConnection(const std::shared_ptr<Connection>& connection)
    {
        connections_.erase(connection);
    }

    xs::core::MainEventLoop& event_loop_;
    xs::core::Logger& logger_;
    GateAuthHttpServiceOptions options_{};
    std::unique_ptr<Tcp::acceptor> acceptor_{};
    std::unordered_set<std::shared_ptr<Connection>> connections_{};
    std::string configured_endpoint_text_{};
    std::string bound_endpoint_{};
    std::string last_error_message_{};
    bool initialized_{false};
    bool running_{false};
};

GateAuthHttpService::GateAuthHttpService(
    xs::core::MainEventLoop& event_loop,
    xs::core::Logger& logger,
    GateAuthHttpServiceOptions options)
    : impl_(std::make_unique<Impl>(event_loop, logger, std::move(options)))
{
}

GateAuthHttpService::~GateAuthHttpService() = default;

NodeErrorCode GateAuthHttpService::Init()
{
    return impl_->Init();
}

NodeErrorCode GateAuthHttpService::Run()
{
    return impl_->Run();
}

NodeErrorCode GateAuthHttpService::Uninit()
{
    if (impl_ != nullptr)
    {
        return impl_->Uninit();
    }

    return NodeErrorCode::None;
}

bool GateAuthHttpService::initialized() const noexcept
{
    return impl_ != nullptr && impl_->initialized();
}

bool GateAuthHttpService::running() const noexcept
{
    return impl_ != nullptr && impl_->running();
}

std::string_view GateAuthHttpService::configured_endpoint() const noexcept
{
    return impl_ != nullptr ? impl_->configured_endpoint() : std::string_view{};
}

std::string_view GateAuthHttpService::bound_endpoint() const noexcept
{
    return impl_ != nullptr ? impl_->bound_endpoint() : std::string_view{};
}

std::string_view GateAuthHttpService::last_error_message() const noexcept
{
    return impl_ != nullptr ? impl_->last_error_message() : std::string_view{};
}

} // namespace xs::node