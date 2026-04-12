using XServer.Client.Entities;

namespace XServer.Client.Runtime;

public sealed partial class GameInstance
{
    public AvatarEntity CreateTemporaryAvatarSelection()
    {
        EnsureAccountReady();

        string accountId = Account!.AccountId;
        Guid avatarEntityId = Guid.NewGuid();
        return new AvatarEntity(avatarEntityId, accountId);
    }

    public AvatarEntity SelectAvatar()
    {
        AvatarEntity avatar = CreateTemporaryAvatarSelection();
        SelectAvatar(avatar);
        return avatar;
    }

    public void SelectAvatar(AvatarEntity avatar)
    {
        ArgumentNullException.ThrowIfNull(avatar);
        EnsureAccountReady();

        if (!string.Equals(Account!.AccountId, avatar.AccountId, StringComparison.Ordinal))
        {
            throw new ArgumentException("Avatar selection account does not match the current Account.", nameof(avatar));
        }

        if (string.IsNullOrWhiteSpace(avatar.AvatarId))
        {
            throw new ArgumentException("Avatar selection id must not be empty.", nameof(avatar));
        }

        if (AvatarSession.SelectedAvatarEntityId.HasValue &&
            AvatarSession.SelectedAvatarEntityId.Value != avatar.EntityId &&
            EntityManager.TryGet<AvatarEntity>(AvatarSession.SelectedAvatarEntityId.Value, out AvatarEntity? previousAvatar) &&
            !AvatarSession.IsSelectionConfirmed)
        {
            previousAvatar.SetRpcSender(null);
            EntityManager.Unregister(previousAvatar.EntityId);
        }

        if (!EntityManager.TryGet<AvatarEntity>(avatar.EntityId, out AvatarEntity? existingAvatar))
        {
            EntityManagerErrorCode registerResult = EntityManager.Register(avatar);
            if (registerResult != EntityManagerErrorCode.None)
            {
                throw new InvalidOperationException(
                    $"Failed to register AvatarEntity {avatar.AvatarId}: {EntityManagerError.Message(registerResult)}");
            }
        }
        else if (!ReferenceEquals(existingAvatar, avatar))
        {
            throw new InvalidOperationException(
                $"Client EntityManager already contains AvatarEntity {avatar.AvatarId}.");
        }

        avatar.SetRpcSender(_rpcSender);
        AvatarSession.Bind(avatar.EntityId);
        RefreshLifecycleState();
    }

    public bool ConfirmAvatarSelection(
        string accountId,
        string avatarId,
        string? gameNodeId = null,
        ulong? sessionId = null)
    {
        EnsureAccountReady();

        if (!Guid.TryParse(avatarId, out Guid avatarEntityId))
        {
            return false;
        }

        AvatarEntity? currentAvatar = Avatar;
        if (currentAvatar is null ||
            !string.Equals(Account!.AccountId, accountId, StringComparison.Ordinal) ||
            currentAvatar.EntityId != avatarEntityId)
        {
            return false;
        }

        AvatarSession.Confirm(gameNodeId, sessionId);
        RefreshLifecycleState();
        return true;
    }

    public void ClearAvatarSelection()
    {
        if (Account is null)
        {
            return;
        }

        Guid? avatarEntityId = AvatarSession.SelectedAvatarEntityId;
        AvatarSession.Clear();
        if (avatarEntityId.HasValue)
        {
            if (EntityManager.TryGet<AvatarEntity>(avatarEntityId.Value, out AvatarEntity? avatar))
            {
                avatar.SetRpcSender(null);
            }

            EntityManager.Unregister(avatarEntityId.Value);
        }

        RefreshLifecycleState();
    }

    public void UpdateAvatarPosition(float x, float y, float z)
    {
        EnsureAvatarReady();
        Avatar!.PositionX = x;
        Avatar.PositionY = y;
        Avatar.PositionZ = z;
    }

    private void EnsureAccountReady()
    {
        if (Account is null)
        {
            throw new InvalidOperationException(
                "The simulated client does not have a local Account yet. Run login <url> <account> <password> first.");
        }
    }

    private void EnsureAvatarReady()
    {
        if (Avatar is null)
        {
            throw new InvalidOperationException(
                "The simulated client does not have a local Avatar bound to an Account yet. Run login <url> <account> <password> and then selectAvatar first.");
        }

        if (!AvatarSession.IsSelectionConfirmed)
        {
            throw new InvalidOperationException(
                "The local Avatar selection is still waiting for server confirmation. Wait for the selectAvatar success response first.");
        }
    }

    private void RefreshLifecycleState()
    {
        if (Avatar is not null && AvatarSession.IsSelectionConfirmed)
        {
            LifecycleState = ClientLifecycleState.AvatarReady;
            return;
        }

        if (Avatar is not null)
        {
            LifecycleState = ClientLifecycleState.AvatarSelecting;
            return;
        }

        if (Account is not null)
        {
            LifecycleState = ClientLifecycleState.LoggedIn;
            return;
        }

        if (Profile is not null)
        {
            LifecycleState = ClientLifecycleState.Connected;
            return;
        }

        LifecycleState = ClientLifecycleState.Disconnected;
    }
}
