namespace XServer.Client.Entities;

public sealed class AvatarEntity : ClientEntity
{
    public AvatarEntity(Guid entityId, string accountId, string displayName)
        : base(entityId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(accountId);
        AccountId = accountId;
        DisplayName = string.IsNullOrWhiteSpace(displayName) ? entityId.ToString("D") : displayName;
    }

    public string AvatarId => EntityId.ToString("D");

    public string AccountId { get; }

    public string DisplayName { get; private set; }

    public float PositionX { get; set; }

    public float PositionY { get; set; }

    public float PositionZ { get; set; }

    public Dictionary<string, int> WeaponInventory { get; } = new(StringComparer.OrdinalIgnoreCase);

    public void UpdateDisplayName(string? displayName)
    {
        if (!string.IsNullOrWhiteSpace(displayName))
        {
            DisplayName = displayName;
        }
    }
}
