using XServer.Client.App;
using XServer.Client.Configuration;

namespace XServer.Client;

internal static class Program
{
    public static async Task<int> Main(string[] args)
    {
        try
        {
            ClientLaunchOptions options = ClientLaunchOptions.Parse(args);
            ClientConsoleApp app = new(options, Console.Out, Console.Error);
            return await app.RunAsync();
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"fatal: {exception.Message}");
            return 1;
        }
    }
}
