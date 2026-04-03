using System.Text;
using XServer.Client.Configuration;

namespace XServer.Client.Tests;

public sealed class ClusterClientConfigLoaderTests
{
    [Fact]
    public void LoadNormalizesWildcardGateHostForDialing()
    {
        string tempDirectory = Path.Combine(Path.GetTempPath(), "xserver-client-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDirectory);

        try
        {
            string configPath = Path.Combine(tempDirectory, "local-dev.json");
            File.WriteAllText(
                configPath,
                """
                {
                  "kcp": {
                    "mtu": 1200,
                    "sndwnd": 128,
                    "rcvwnd": 128,
                    "nodelay": true,
                    "intervalMs": 10,
                    "fastResend": 2,
                    "noCongestionWindow": false,
                    "minRtoMs": 30,
                    "deadLinkCount": 20,
                    "streamMode": false
                  },
                  "gate": {
                    "Gate0": {
                      "clientNetwork": {
                        "listenEndpoint": {
                          "host": "0.0.0.0",
                          "port": 4000
                        }
                      }
                    }
                  }
                }
                """,
                new UTF8Encoding(false));

            ResolvedClientProfile profile = ClusterClientConfigLoader.Load(
                new ClientLaunchOptions(configPath, "Gate0", null, null, 1U, null, false));

            Assert.Equal("127.0.0.1", profile.Host);
            Assert.Equal(4000, profile.Port);
            Assert.Contains("normalized", profile.EndpointSource, StringComparison.OrdinalIgnoreCase);
        }
        finally
        {
            if (Directory.Exists(tempDirectory))
            {
                Directory.Delete(tempDirectory, recursive: true);
            }
        }
    }
}
