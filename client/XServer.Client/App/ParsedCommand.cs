using System.Globalization;

namespace XServer.Client.App;

public sealed class ParsedCommand
{
    private readonly Dictionary<string, string> _options;

    private ParsedCommand(
        string name,
        IReadOnlyList<string> positionals,
        Dictionary<string, string> options,
        string rawText)
    {
        Name = name;
        Positionals = positionals;
        _options = options;
        RawText = rawText;
    }

    public string Name { get; }
    public IReadOnlyList<string> Positionals { get; }
    public IReadOnlyDictionary<string, string> Options => _options;
    public string RawText { get; }

    public static bool TryParse(string line, out ParsedCommand? command, out string? error)
    {
        command = null;
        if (!CommandTokenizer.TryTokenize(line, out IReadOnlyList<string> tokens, out error))
        {
            return false;
        }

        if (tokens.Count == 0)
        {
            error = null;
            return true;
        }

        string name = tokens[0];
        List<string> positionals = [];
        Dictionary<string, string> options = new(StringComparer.OrdinalIgnoreCase);

        for (int index = 1; index < tokens.Count; index++)
        {
            string token = tokens[index];
            int separatorIndex = token.IndexOf('=');
            if (separatorIndex > 0)
            {
                string key = token[..separatorIndex];
                string value = token[(separatorIndex + 1)..];
                if (!options.TryAdd(key, value))
                {
                    error = $"Duplicate command option '{key}'.";
                    return false;
                }
            }
            else
            {
                positionals.Add(token);
            }
        }

        command = new ParsedCommand(name, positionals, options, line.Trim());
        error = null;
        return true;
    }

    public bool HasOption(string name)
    {
        return _options.ContainsKey(name);
    }

    public string GetStringOrDefault(string name, string defaultValue)
    {
        return _options.TryGetValue(name, out string? value) ? value : defaultValue;
    }

    public string? GetOptionalString(string name)
    {
        return _options.TryGetValue(name, out string? value) ? value : null;
    }

    public string GetRequiredString(string name)
    {
        if (!_options.TryGetValue(name, out string? value) || string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException($"Missing required option '{name}'.");
        }

        return value;
    }

    public bool GetBooleanOrDefault(string name, bool defaultValue)
    {
        if (!_options.TryGetValue(name, out string? value))
        {
            return defaultValue;
        }

        return value.ToLowerInvariant() switch
        {
            "1" or "true" or "yes" or "y" or "on" => true,
            "0" or "false" or "no" or "n" or "off" => false,
            _ => throw new ArgumentException($"Option '{name}' expects a boolean value but received '{value}'."),
        };
    }

    public int GetInt32OrDefault(string name, int defaultValue)
    {
        if (!_options.TryGetValue(name, out string? value))
        {
            return defaultValue;
        }

        if (!int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int parsedValue))
        {
            throw new ArgumentException($"Option '{name}' expects an integer value but received '{value}'.");
        }

        return parsedValue;
    }

    public uint GetUInt32OrDefault(string name, uint defaultValue)
    {
        if (!_options.TryGetValue(name, out string? value))
        {
            return defaultValue;
        }

        return ParseUInt32(name, value);
    }

    public uint GetRequiredUInt32(string name)
    {
        if (!_options.TryGetValue(name, out string? value))
        {
            throw new ArgumentException($"Missing required option '{name}'.");
        }

        return ParseUInt32(name, value);
    }

    public float GetSingleOrDefault(string name, float defaultValue)
    {
        if (!_options.TryGetValue(name, out string? value))
        {
            return defaultValue;
        }

        if (!float.TryParse(value, NumberStyles.Float | NumberStyles.AllowThousands, CultureInfo.InvariantCulture, out float parsedValue))
        {
            throw new ArgumentException($"Option '{name}' expects a floating-point value but received '{value}'.");
        }

        return parsedValue;
    }

    private static uint ParseUInt32(string name, string value)
    {
        if (value.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            if (uint.TryParse(value[2..], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint hexValue))
            {
                return hexValue;
            }
        }
        else if (uint.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out uint decimalValue))
        {
            return decimalValue;
        }

        throw new ArgumentException($"Option '{name}' expects an unsigned integer value but received '{value}'.");
    }
}
