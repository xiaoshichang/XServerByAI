using System.Text;
using XServer.Client.Configuration;

namespace XServer.Client.Tests;

public sealed class ClusterClientConfigLoaderTests
{
    [Fact]
    public void LoadNormalizesWildcardGateHostsForDialing()
    {
        string tempDirectory = CreateTempDirectory();

        try
        {
            string configPath = WriteConfig(
                tempDirectory,
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
                      "authNetwork": {
                        "listenEndpoint": {
                          "host": "0.0.0.0",
                          "port": 4100
                        }
                      },
                      "clientNetwork": {
                        "listenEndpoint": {
                          "host": "0.0.0.0",
                          "port": 4000
                        }
                      }
                    }
                  }
                }
                """);

            ResolvedClientProfile profile = ClusterClientConfigLoader.Load(
                new ClientLaunchOptions(configPath, "Gate0", null, null, 1U, null, false));

            Assert.Equal("127.0.0.1", profile.Host);
            Assert.Equal(4000, profile.Port);
            Assert.Equal("127.0.0.1", profile.AuthHost);
            Assert.Equal(4100, profile.AuthPort);
            Assert.Contains("normalized", profile.EndpointSource, StringComparison.OrdinalIgnoreCase);
        }
        finally
        {
            DeleteDirectory(tempDirectory);
        }
    }

    [Fact]
    public void LoadRejectsConfigWithoutAuthNetwork()
    {
        string tempDirectory = CreateTempDirectory();

        try
        {
            string configPath = WriteConfig(
                tempDirectory,
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
                          "host": "127.0.0.1",
                          "port": 4000
                        }
                      }
                    }
                  }
                }
                """);

            InvalidDataException exception = Assert.Throws<InvalidDataException>(
                () => ClusterClientConfigLoader.Load(
                    new ClientLaunchOptions(configPath, "Gate0", null, null, 1U, null, false)));

            Assert.Contains("gate.Gate0.authNetwork", exception.Message, StringComparison.Ordinal);
        }
        finally
        {
            DeleteDirectory(tempDirectory);
        }
    }

    private static string CreateTempDirectory()
    {
        string tempDirectory = Path.Combine(Path.GetTempPath(), "xserver-client-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDirectory);
        return tempDirectory;
    }

    private static string WriteConfig(string tempDirectory, string text)
    {
        string configPath = Path.Combine(tempDirectory, "local-dev.json");
        File.WriteAllText(configPath, text, new UTF8Encoding(false));
        return configPath;
    }

    private static void DeleteDirectory(string path)
    {
        if (Directory.Exists(path))
        {
            Directory.Delete(path, recursive: true);
        }
    }
}
