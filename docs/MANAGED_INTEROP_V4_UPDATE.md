# Managed Interop V4 Update

Date: 2026-03-30

This note records the managed-to-native log bridge added on top of the ownership and ready callback flow.

## ABI Version

- The active managed interop ABI is now `v4`.
- `ManagedNativeCallbacks` now carries both the server-stub-ready callback and a managed log callback.

## Managed Log Bridge

The callback table now includes:

- `ManagedNativeCallbacks.on_server_stub_ready`
- `ManagedNativeCallbacks.on_log`

The managed log callback payload includes:

- native callback context
- managed log level
- UTF-8 log category bytes plus length
- UTF-8 log message bytes plus length

## Runtime Flow

1. `GameNode::InitializeManagedRuntime()` passes the native callback table into `GameNativeInit`.
2. Managed `GameNativeInit` stores the callbacks and configures `NativeLoggerBridge`.
3. Managed runtime log points forward their level/category/message back into the native `Logger`.
4. Ownership apply/reset and ready callback paths now emit minimal managed-runtime logs through the same bridge.

## Compatibility Notes

- `GM` still loads `GameLogic` only for server-stub catalog discovery and does not execute `GameNativeInit`.
- The log bridge is best-effort only: if no native log callback is provided or UTF-8 encoding fails, managed runtime behavior still continues.
- The existing ready callback ABI and ready-entry query exports remain unchanged apart from the ABI version bump.
