namespace XServer.Client.Runtime;

public sealed class AvatarView
{
    public required string AccountId { get; init; }
    public required string AvatarId { get; init; }
    public required string DisplayName { get; init; }
    public bool IsServerConfirmed { get; private set; }
    public float PositionX { get; set; }
    public float PositionY { get; set; }
    public float PositionZ { get; set; }
    public Dictionary<string, int> WeaponInventory { get; } = new(StringComparer.OrdinalIgnoreCase);

    public void MarkServerConfirmed()
    {
        IsServerConfirmed = true;
    }
}
