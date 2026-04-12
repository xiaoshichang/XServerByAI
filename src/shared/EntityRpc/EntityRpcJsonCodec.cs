using System.Globalization;
using System.Text.Json;

#if XSERVER_CLIENT_FRAMEWORK
namespace XServer.Client.Rpc;
#elif XSERVER_MANAGED_FRAMEWORK
namespace XServer.Managed.Framework.Rpc;
#else
#error EntityRpc shared sources must be compiled by a framework project.
#endif

public static class EntityRpcJsonCodec
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    };

    public static byte[] Encode(Guid entityId, string rpcName, params object?[] arguments)
    {
        if (entityId == Guid.Empty)
        {
            throw new ArgumentException("RPC entityId must not be empty.", nameof(entityId));
        }

        ArgumentException.ThrowIfNullOrWhiteSpace(rpcName);
        ArgumentNullException.ThrowIfNull(arguments);

        foreach (object? argument in arguments)
        {
            if (argument is not null && !IsSupportedParameterType(argument.GetType()))
            {
                throw new ArgumentException(
                    $"RPC argument type '{argument.GetType().FullName}' is not supported by the JSON RPC codec.",
                    nameof(arguments));
            }
        }

        SerializableEntityRpcEnvelope envelope = new()
        {
            EntityId = entityId.ToString("D"),
            RpcName = rpcName,
            Arguments = arguments,
        };

        return JsonSerializer.SerializeToUtf8Bytes(envelope, JsonOptions);
    }

    public static bool TryDecode(
        ReadOnlyMemory<byte> payload,
        out EntityRpcInvocationEnvelope envelope,
        out EntityRpcDispatchErrorCode errorCode,
        out string errorMessage)
    {
        envelope = default;
        errorCode = EntityRpcDispatchErrorCode.None;
        errorMessage = string.Empty;

        if (payload.IsEmpty)
        {
            errorCode = EntityRpcDispatchErrorCode.InvalidPayload;
            errorMessage = "RPC payload must not be empty.";
            return false;
        }

        try
        {
            using JsonDocument document = JsonDocument.Parse(payload);
            JsonElement root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
            {
                errorCode = EntityRpcDispatchErrorCode.InvalidPayload;
                errorMessage = "RPC payload root must be a JSON object.";
                return false;
            }

            if (!TryGetProperty(root, "entityId", out JsonElement entityIdElement) ||
                entityIdElement.ValueKind != JsonValueKind.String ||
                !Guid.TryParse(entityIdElement.GetString(), out Guid entityId) ||
                entityId == Guid.Empty)
            {
                errorCode = EntityRpcDispatchErrorCode.InvalidPayload;
                errorMessage = "RPC payload entityId must be a non-empty GUID string.";
                return false;
            }

            if (!TryGetProperty(root, "rpcName", out JsonElement rpcNameElement) ||
                rpcNameElement.ValueKind != JsonValueKind.String ||
                string.IsNullOrWhiteSpace(rpcNameElement.GetString()))
            {
                errorCode = EntityRpcDispatchErrorCode.InvalidPayload;
                errorMessage = "RPC payload rpcName must not be empty.";
                return false;
            }

            List<JsonElement> arguments = [];
            if (TryGetProperty(root, "arguments", out JsonElement argumentsElement))
            {
                if (argumentsElement.ValueKind != JsonValueKind.Array)
                {
                    errorCode = EntityRpcDispatchErrorCode.InvalidPayload;
                    errorMessage = "RPC payload arguments must be a JSON array.";
                    return false;
                }

                foreach (JsonElement argument in argumentsElement.EnumerateArray())
                {
                    arguments.Add(argument.Clone());
                }
            }

            envelope = new EntityRpcInvocationEnvelope(entityId, rpcNameElement.GetString()!, arguments);
            return true;
        }
        catch (JsonException exception)
        {
            errorCode = EntityRpcDispatchErrorCode.InvalidPayload;
            errorMessage = $"RPC payload JSON decode failed: {exception.Message}";
            return false;
        }
    }

    public static bool TryBindArguments(
        EntityRpcInvocationEnvelope envelope,
        IReadOnlyList<Type> parameterTypes,
        out object?[] arguments,
        out EntityRpcDispatchErrorCode errorCode,
        out string errorMessage)
    {
        arguments = [];
        errorCode = EntityRpcDispatchErrorCode.None;
        errorMessage = string.Empty;

        if (parameterTypes.Count != envelope.Arguments.Count)
        {
            errorCode = EntityRpcDispatchErrorCode.InvalidArgumentCount;
            errorMessage =
                $"RPC '{envelope.RpcName}' expected {parameterTypes.Count} arguments but received {envelope.Arguments.Count}.";
            return false;
        }

        arguments = new object?[parameterTypes.Count];
        for (int index = 0; index < parameterTypes.Count; index++)
        {
            Type parameterType = parameterTypes[index];
            if (!IsSupportedParameterType(parameterType))
            {
                errorCode = EntityRpcDispatchErrorCode.UnsupportedParameterType;
                errorMessage =
                    $"RPC '{envelope.RpcName}' parameter type '{parameterType.FullName}' is not supported.";
                arguments = [];
                return false;
            }

            if (!TryConvertArgument(envelope.Arguments[index], parameterType, out object? value))
            {
                errorCode = EntityRpcDispatchErrorCode.InvalidArgumentType;
                errorMessage =
                    $"RPC '{envelope.RpcName}' argument {index} could not be converted to '{parameterType.FullName}'.";
                arguments = [];
                return false;
            }

            arguments[index] = value;
        }

        return true;
    }

    public static bool IsSupportedParameterType(Type parameterType)
    {
        ArgumentNullException.ThrowIfNull(parameterType);

        Type effectiveType = Nullable.GetUnderlyingType(parameterType) ?? parameterType;
        if (effectiveType == typeof(string) || effectiveType == typeof(Guid))
        {
            return true;
        }

        TypeCode typeCode = Type.GetTypeCode(effectiveType);
        return typeCode is TypeCode.Boolean or
            TypeCode.Byte or
            TypeCode.SByte or
            TypeCode.Int16 or
            TypeCode.UInt16 or
            TypeCode.Int32 or
            TypeCode.UInt32 or
            TypeCode.Int64 or
            TypeCode.UInt64 or
            TypeCode.Single or
            TypeCode.Double or
            TypeCode.Decimal;
    }

    private static bool TryConvertArgument(JsonElement element, Type parameterType, out object? value)
    {
        value = null;

        Type? underlyingNullableType = Nullable.GetUnderlyingType(parameterType);
        Type effectiveType = underlyingNullableType ?? parameterType;

        if (element.ValueKind == JsonValueKind.Null)
        {
            return !effectiveType.IsValueType || underlyingNullableType is not null;
        }

        try
        {
            if (effectiveType == typeof(string))
            {
                if (element.ValueKind != JsonValueKind.String)
                {
                    return false;
                }

                value = element.GetString();
                return true;
            }

            if (effectiveType == typeof(Guid))
            {
                if (element.ValueKind != JsonValueKind.String ||
                    !Guid.TryParse(element.GetString(), out Guid guidValue))
                {
                    return false;
                }

                value = guidValue;
                return true;
            }

            if (effectiveType == typeof(bool))
            {
                if (element.ValueKind is not JsonValueKind.True and not JsonValueKind.False)
                {
                    return false;
                }

                value = element.GetBoolean();
                return true;
            }

            if (effectiveType == typeof(byte) && element.TryGetByte(out byte byteValue))
            {
                value = byteValue;
                return true;
            }

            if (effectiveType == typeof(sbyte) && element.TryGetSByte(out sbyte sbyteValue))
            {
                value = sbyteValue;
                return true;
            }

            if (effectiveType == typeof(short) && element.TryGetInt16(out short int16Value))
            {
                value = int16Value;
                return true;
            }

            if (effectiveType == typeof(ushort) && element.TryGetUInt16(out ushort uint16Value))
            {
                value = uint16Value;
                return true;
            }

            if (effectiveType == typeof(int) && element.TryGetInt32(out int int32Value))
            {
                value = int32Value;
                return true;
            }

            if (effectiveType == typeof(uint) && element.TryGetUInt32(out uint uint32Value))
            {
                value = uint32Value;
                return true;
            }

            if (effectiveType == typeof(long) && element.TryGetInt64(out long int64Value))
            {
                value = int64Value;
                return true;
            }

            if (effectiveType == typeof(ulong) && element.TryGetUInt64(out ulong uint64Value))
            {
                value = uint64Value;
                return true;
            }

            if (effectiveType == typeof(float) && element.TryGetSingle(out float singleValue))
            {
                value = singleValue;
                return true;
            }

            if (effectiveType == typeof(double) && element.TryGetDouble(out double doubleValue))
            {
                value = doubleValue;
                return true;
            }

            if (effectiveType == typeof(decimal) && element.TryGetDecimal(out decimal decimalValue))
            {
                value = decimalValue;
                return true;
            }

            if (effectiveType == typeof(float) &&
                element.ValueKind == JsonValueKind.String &&
                float.TryParse(element.GetString(), NumberStyles.Float, CultureInfo.InvariantCulture, out singleValue))
            {
                value = singleValue;
                return true;
            }

            if (effectiveType == typeof(double) &&
                element.ValueKind == JsonValueKind.String &&
                double.TryParse(element.GetString(), NumberStyles.Float, CultureInfo.InvariantCulture, out doubleValue))
            {
                value = doubleValue;
                return true;
            }

            if (effectiveType == typeof(decimal) &&
                element.ValueKind == JsonValueKind.String &&
                decimal.TryParse(element.GetString(), NumberStyles.Float, CultureInfo.InvariantCulture, out decimalValue))
            {
                value = decimalValue;
                return true;
            }
        }
        catch (FormatException)
        {
            return false;
        }

        return false;
    }

    private static bool TryGetProperty(JsonElement element, string propertyName, out JsonElement value)
    {
        if (element.TryGetProperty(propertyName, out value))
        {
            return true;
        }

        foreach (JsonProperty property in element.EnumerateObject())
        {
            if (string.Equals(property.Name, propertyName, StringComparison.OrdinalIgnoreCase))
            {
                value = property.Value;
                return true;
            }
        }

        value = default;
        return false;
    }

    private sealed class SerializableEntityRpcEnvelope
    {
        public string? EntityId { get; init; }

        public string? RpcName { get; init; }

        public object?[] Arguments { get; init; } = [];
    }
}
