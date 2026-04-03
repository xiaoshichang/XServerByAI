using XServer.Client.App;

namespace XServer.Client.Tests;

public sealed class ParsedCommandTests
{
    [Fact]
    public void ParseSupportsQuotedKeyValuePairs()
    {
        bool success = ParsedCommand.TryParse(
            "send msgId=0xAFF text=\"hello client world\" flags=response,error",
            out ParsedCommand? command,
            out string? error);

        Assert.True(success);
        Assert.Null(error);
        Assert.NotNull(command);
        Assert.Equal("send", command.Name);
        Assert.Equal(0xAFFU, command.GetRequiredUInt32("msgId"));
        Assert.Equal("hello client world", command.GetRequiredString("text"));
        Assert.Equal("response,error", command.GetRequiredString("flags"));
    }

    [Fact]
    public void ParseTreatsCommentAsNoOp()
    {
        bool success = ParsedCommand.TryParse("# comment", out ParsedCommand? command, out string? error);

        Assert.True(success);
        Assert.Null(error);
        Assert.Null(command);
    }
}
