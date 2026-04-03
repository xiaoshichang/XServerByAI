namespace XServer.Client.App;

public static class ClientScriptRunner
{
    public static async Task<bool> RunAsync(
        string scriptPath,
        Func<ParsedCommand, CancellationToken, Task<bool>> executeCommandAsync,
        Action<string> writeInfo,
        Action<string> writeError,
        bool continueOnError,
        CancellationToken cancellationToken)
    {
        string fullPath = Path.GetFullPath(scriptPath);
        if (!File.Exists(fullPath))
        {
            throw new FileNotFoundException($"Script file was not found: {fullPath}", fullPath);
        }

        string[] lines = await File.ReadAllLinesAsync(fullPath, cancellationToken);
        for (int index = 0; index < lines.Length; index++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            string line = lines[index];
            if (!ParsedCommand.TryParse(line, out ParsedCommand? command, out string? parseError))
            {
                string message = $"script {Path.GetFileName(fullPath)}:{index + 1}: {parseError}";
                writeError(message);
                if (!continueOnError)
                {
                    break;
                }

                continue;
            }

            if (command is null)
            {
                continue;
            }

            writeInfo($"script> {command.RawText}");

            try
            {
                bool continueApplication = await executeCommandAsync(command, cancellationToken);
                if (!continueApplication)
                {
                    return false;
                }
            }
            catch (Exception exception)
            {
                writeError($"script {Path.GetFileName(fullPath)}:{index + 1}: {exception.Message}");
                if (!continueOnError)
                {
                    break;
                }
            }
        }

        return true;
    }
}
