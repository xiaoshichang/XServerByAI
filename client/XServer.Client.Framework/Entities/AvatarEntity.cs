using XServer.Client.Rpc;

namespace XServer.Client.Entities;

public sealed class AvatarEntity : ClientEntity
{
    public AvatarEntity(Guid entityId, string accountId)
        : base(entityId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(accountId);
        AccountId = accountId;
    }

    public string AvatarId => EntityId.ToString("D");

    public string AccountId { get; }

    public float PositionX { get; set; }

    public float PositionY { get; set; }

    public float PositionZ { get; set; }

    public string Weapon { get; private set; } = string.Empty;

    [ClientRPC]
    public void OnSetWeaponResult(string weapon, bool succ)
    {
        if (succ && !string.IsNullOrWhiteSpace(weapon))
        {
            Weapon = weapon;
        }
    }
}
