using System;

namespace XServer.Managed.Framework.Entities
{
    [AttributeUsage(AttributeTargets.Property, AllowMultiple = false, Inherited = true)]
    public sealed class EntityPropertyAttribute : Attribute
    {
        public EntityPropertyAttribute(EntityPropertyFlags flags)
        {
            Flags = flags;
        }

        public EntityPropertyFlags Flags { get; }
    }
}
