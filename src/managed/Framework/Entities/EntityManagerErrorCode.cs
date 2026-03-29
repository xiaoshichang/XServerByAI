namespace XServer.Managed.Framework.Entities
{
    public enum EntityManagerErrorCode
    {
        None = 0,
        InvalidArgument = 1,
        DuplicateEntityId = 2,
        EntityNotFound = 3,
    }

    public static class EntityManagerError
    {
        public static string Message(EntityManagerErrorCode code)
        {
            return code switch
            {
                EntityManagerErrorCode.None => "No error.",
                EntityManagerErrorCode.InvalidArgument => "Entity manager argument is invalid.",
                EntityManagerErrorCode.DuplicateEntityId => "Entity manager already contains the entity ID.",
                EntityManagerErrorCode.EntityNotFound => "Entity manager could not find the requested entity.",
                _ => "Unknown entity manager error.",
            };
        }
    }
}
