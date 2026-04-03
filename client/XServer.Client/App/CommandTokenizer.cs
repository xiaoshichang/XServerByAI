using System.Text;

namespace XServer.Client.App;

public static class CommandTokenizer
{
    public static bool TryTokenize(string line, out IReadOnlyList<string> tokens, out string? error)
    {
        tokens = Array.Empty<string>();
        error = null;

        if (string.IsNullOrWhiteSpace(line))
        {
            return true;
        }

        string trimmedLine = line.TrimStart();
        if (trimmedLine.StartsWith('#'))
        {
            return true;
        }

        List<string> parsedTokens = [];
        StringBuilder currentToken = new();
        bool inQuotes = false;
        bool escaping = false;

        foreach (char character in line)
        {
            if (escaping)
            {
                currentToken.Append(character);
                escaping = false;
                continue;
            }

            if (inQuotes && character == '\\')
            {
                escaping = true;
                continue;
            }

            if (character == '"')
            {
                inQuotes = !inQuotes;
                continue;
            }

            if (!inQuotes && char.IsWhiteSpace(character))
            {
                if (currentToken.Length > 0)
                {
                    parsedTokens.Add(currentToken.ToString());
                    currentToken.Clear();
                }

                continue;
            }

            currentToken.Append(character);
        }

        if (escaping)
        {
            error = "Command line ends with an unfinished escape sequence.";
            return false;
        }

        if (inQuotes)
        {
            error = "Command line has an unmatched double quote.";
            return false;
        }

        if (currentToken.Length > 0)
        {
            parsedTokens.Add(currentToken.ToString());
        }

        tokens = parsedTokens;
        return true;
    }
}
