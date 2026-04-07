using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using XServer.Client.Auth;

namespace XServer.Client.Tests;

public sealed class GateAuthClientTests
{
    [Fact]
    public async Task LoginAsyncPostsCredentialsAndParsesGrant()
    {
        CapturingHttpMessageHandler handler = new((request, cancellationToken) =>
        {
            HttpResponseMessage response = new(HttpStatusCode.OK)
            {
                Content = new StringContent(
                    """
                    {
                      "gateNodeId": "Gate0",
                      "account": "demo-account",
                      "kcp": {
                        "host": "127.0.0.1",
                        "port": 4000,
                        "conversation": 321
                      },
                      "issuedAtUnixMs": 1712131200000,
                      "expiresAtUnixMs": 1712131230000
                    }
                    """,
                    Encoding.UTF8,
                    "application/json"),
            };
            return Task.FromResult(response);
        });

        using HttpClient httpClient = new(handler);
        GateAuthClient client = new(httpClient);

        GateLoginGrant grant = await client.LoginAsync(
            "http://127.0.0.1:4100",
            "Gate0",
            "demo-account",
            "secret",
            CancellationToken.None);

        Assert.NotNull(handler.LastRequest);
        Assert.Equal(HttpMethod.Post, handler.LastRequest!.Method);
        Assert.Equal("http://127.0.0.1:4100/login", handler.LastRequest.RequestUri!.ToString());
        Assert.Equal("application/json", handler.LastRequest.Content!.Headers.ContentType!.MediaType);

        string requestBody = await handler.LastRequest.Content.ReadAsStringAsync();
        using JsonDocument requestDocument = JsonDocument.Parse(requestBody);
        Assert.Equal("demo-account", requestDocument.RootElement.GetProperty("account").GetString());
        Assert.Equal("secret", requestDocument.RootElement.GetProperty("password").GetString());

        Assert.Equal("Gate0", grant.GateNodeId);
        Assert.Equal("demo-account", grant.AccountId);
        Assert.Equal("127.0.0.1", grant.KcpHost);
        Assert.Equal(4000, grant.KcpPort);
        Assert.Equal(321U, grant.Conversation);
        Assert.Equal(DateTimeOffset.FromUnixTimeMilliseconds(1712131200000), grant.IssuedAt);
        Assert.Equal(DateTimeOffset.FromUnixTimeMilliseconds(1712131230000), grant.ExpiresAt);
    }

    [Fact]
    public async Task LoginAsyncSurfacesJsonErrorBody()
    {
        CapturingHttpMessageHandler handler = new((request, cancellationToken) =>
        {
            HttpResponseMessage response = new(HttpStatusCode.Unauthorized)
            {
                Content = new StringContent(
                    """
                    {
                      "error": "invalid credentials"
                    }
                    """,
                    Encoding.UTF8,
                    "application/json"),
            };
            return Task.FromResult(response);
        });

        using HttpClient httpClient = new(handler);
        GateAuthClient client = new(httpClient);

        InvalidOperationException exception = await Assert.ThrowsAsync<InvalidOperationException>(
            () => client.LoginAsync(
                "http://127.0.0.1:4100",
                "Gate0",
                "demo-account",
                "bad-secret",
                CancellationToken.None));

        Assert.Contains("status 401", exception.Message, StringComparison.Ordinal);
        Assert.Contains("invalid credentials", exception.Message, StringComparison.Ordinal);
    }

    private sealed class CapturingHttpMessageHandler : HttpMessageHandler
    {
        private readonly Func<HttpRequestMessage, CancellationToken, Task<HttpResponseMessage>> _responder;

        public CapturingHttpMessageHandler(Func<HttpRequestMessage, CancellationToken, Task<HttpResponseMessage>> responder)
        {
            _responder = responder;
        }

        public HttpRequestMessage? LastRequest { get; private set; }

        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            LastRequest = CloneRequest(request);
            return _responder(request, cancellationToken);
        }

        private static HttpRequestMessage CloneRequest(HttpRequestMessage request)
        {
            HttpRequestMessage clone = new(request.Method, request.RequestUri);

            if (request.Content is not null)
            {
                string content = request.Content.ReadAsStringAsync().GetAwaiter().GetResult();
                StringContent clonedContent = new(content, Encoding.UTF8);

                MediaTypeHeaderValue? contentType = request.Content.Headers.ContentType;
                if (contentType is not null)
                {
                    clonedContent.Headers.ContentType = new MediaTypeHeaderValue(contentType.MediaType!);
                    if (!string.IsNullOrWhiteSpace(contentType.CharSet))
                    {
                        clonedContent.Headers.ContentType.CharSet = contentType.CharSet;
                    }
                }

                clone.Content = clonedContent;
            }

            foreach (KeyValuePair<string, IEnumerable<string>> header in request.Headers)
            {
                clone.Headers.TryAddWithoutValidation(header.Key, header.Value);
            }

            return clone;
        }
    }
}
