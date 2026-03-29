using System;

namespace XServer.Managed.Framework.Entities
{
    [AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = false)]
    public sealed class EntityPropertyAttribute : Attribute
    {
        public EntityPropertyAttribute()
            : this((EntityPropertyFlags)0)
        {
        }

        public EntityPropertyAttribute(EntityPropertyFlags flags)
        {
            Flags = flags;
        }

        public EntityPropertyFlags Flags { get; }
    }
}
