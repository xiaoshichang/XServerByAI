using System.Net.Http;
using System.Text;
using System.Text.Json;

namespace XServer.Client.Auth;

public sealed class GateAuthClient
{
    private readonly HttpClient _httpClient;

    public GateAuthClient(HttpClient httpClient)
    {
        _httpClient = httpClient ?? throw new ArgumentNullException(nameof(httpClient));
    }

    public async Task<GateLoginGrant> LoginAsync(
        string url,
        string fallbackGateNodeId,
        string accountId,
        string password,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(url);
        ArgumentException.ThrowIfNullOrWhiteSpace(fallbackGateNodeId);
        ArgumentException.ThrowIfNullOrWhiteSpace(accountId);
        ArgumentException.ThrowIfNullOrWhiteSpace(password);

        using HttpRequestMessage request = new(HttpMethod.Post, BuildLoginUri(url))
        {
            Content = new StringContent(
                JsonSerializer.Serialize(new { account = accountId, password }),
                Encoding.UTF8,
                "application/json"),
        };

        using HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken);
        string body = await response.Content.ReadAsStringAsync(cancellationToken);
        if (!response.IsSuccessStatusCode)
        {
            throw new InvalidOperationException(BuildErrorMessage((int)response.StatusCode, body));
        }

        return ParseGrant(body, fallbackGateNodeId);
    }

    private static Uri BuildLoginUri(string url)
    {
        if (!Uri.TryCreate(url, UriKind.Absolute, out Uri? uri))
        {
            throw new ArgumentException($"Invalid login url '{url}'.", nameof(url));
        }

        if (!string.Equals(uri.Scheme, Uri.UriSchemeHttp, StringComparison.OrdinalIgnoreCase) &&
            !string.Equals(uri.Scheme, Uri.UriSchemeHttps, StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException("login url must use http or https.", nameof(url));
        }

        if (string.IsNullOrEmpty(uri.AbsolutePath) || uri.AbsolutePath == "/")
        {
            UriBuilder builder = new(uri)
            {
                Path = "/login",
            };
            return builder.Uri;
        }

        return uri;
    }

    private static GateLoginGrant ParseGrant(string body, string fallbackGateNodeId)
    {
        using JsonDocument document = JsonDocument.Parse(body);
        JsonElement root = document.RootElement;
        JsonElement kcpElement = GetRequiredProperty(root, "kcp", "root");

        string gateNodeId = TryGetOptionalString(root, "gateNodeId") ?? fallbackGateNodeId;
        string accountId = GetRequiredString(root, "account", "root");
        string kcpHost = GetRequiredString(kcpElement, "host", "root.kcp");
        int kcpPort = GetRequiredPositiveInt32(kcpElement, "port", "root.kcp");
        uint conversation = GetRequiredPositiveUInt32(kcpElement, "conversation", "root.kcp");
        long issuedAtUnixMs = GetRequiredInt64(root, "issuedAtUnixMs", "root");
        long expiresAtUnixMs = GetRequiredInt64(root, "expiresAtUnixMs", "root");

        return new GateLoginGrant(
            gateNodeId,
            accountId,
            kcpHost,
            kcpPort,
            conversation,
            DateTimeOffset.FromUnixTimeMilliseconds(issuedAtUnixMs),
            DateTimeOffset.FromUnixTimeMilliseconds(expiresAtUnixMs));
    }

    private static string BuildErrorMessage(int statusCode, string body)
    {
        string? serverError = null;
        if (!string.IsNullOrWhiteSpace(body))
        {
            try
            {
                using JsonDocument document = JsonDocument.Parse(body);
                serverError = TryGetOptionalString(document.RootElement, "error");
            }
            catch (JsonException)
            {
            }
        }

        return serverError is null
            ? $"Gate HTTP login failed with status {statusCode}."
            : $"Gate HTTP login failed with status {statusCode}: {serverError}";
    }

    private static JsonElement GetRequiredProperty(JsonElement element, string propertyName, string path)
    {
        if (!element.TryGetProperty(propertyName, out JsonElement property))
        {
            throw new InvalidDataException($"Missing required property '{path}.{propertyName}'.");
        }

        return property;
    }

    private static string GetRequiredString(JsonElement element, string propertyName, string path)
    {
        JsonElement property = GetRequiredProperty(element, propertyName, path);
        string? value = property.GetString();
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new InvalidDataException($"Property '{path}.{propertyName}' must not be empty.");
        }

        return value;
    }

    private static string? TryGetOptionalString(JsonElement element, string propertyName)
    {
        if (!element.TryGetProperty(propertyName, out JsonElement property) || property.ValueKind != JsonValueKind.String)
        {
            return null;
        }

        return property.GetString();
    }

    private static int GetRequiredPositiveInt32(JsonElement element, string propertyName, string path)
    {
        JsonElement property = GetRequiredProperty(element, propertyName, path);
        if (!property.TryGetInt32(out int value) || value <= 0)
        {
            throw new InvalidDataException($"Property '{path}.{propertyName}' must be a positive integer.");
        }

        return value;
    }

    private static long GetRequiredInt64(JsonElement element, string propertyName, string path)
    {
        JsonElement property = GetRequiredProperty(element, propertyName, path);
        if (!property.TryGetInt64(out long value))
        {
            throw new InvalidDataException($"Property '{path}.{propertyName}' must be an integer.");
        }

        return value;
    }

    private static uint GetRequiredPositiveUInt32(JsonElement element, string propertyName, string path)
    {
        JsonElement property = GetRequiredProperty(element, propertyName, path);
        if (!property.TryGetUInt32(out uint value) || value == 0U)
        {
            throw new InvalidDataException($"Property '{path}.{propertyName}' must be a positive unsigned integer.");
        }

        return value;
    }
}
