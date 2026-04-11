namespace XServer.Client.Runtime;

public sealed class AvatarSessionState
{
    public Guid? SelectedAvatarEntityId { get; private set; }

    public bool IsSelectionConfirmed { get; private set; }

    public string? GameNodeId { get; private set; }

    public ulong? SessionId { get; private set; }

    public bool HasSelection => SelectedAvatarEntityId.HasValue;

    public void Bind(Guid avatarEntityId)
    {
        if (avatarEntityId == Guid.Empty)
        {
            throw new ArgumentException("Avatar entityId must not be empty.", nameof(avatarEntityId));
        }

        SelectedAvatarEntityId = avatarEntityId;
        IsSelectionConfirmed = false;
        GameNodeId = null;
        SessionId = null;
    }

    public void Confirm(string? gameNodeId = null, ulong? sessionId = null)
    {
        if (!SelectedAvatarEntityId.HasValue)
        {
            throw new InvalidOperationException("Cannot confirm avatar selection before a local avatar is bound.");
        }

        IsSelectionConfirmed = true;
        GameNodeId = string.IsNullOrWhiteSpace(gameNodeId) ? null : gameNodeId;
        SessionId = sessionId;
    }

    public void Clear()
    {
        SelectedAvatarEntityId = null;
        IsSelectionConfirmed = false;
        GameNodeId = null;
        SessionId = null;
    }
}
