# Managed Interop V3 Update

Date: 2026-03-29

This note records the managed/native callback bridge added on top of the ownership and ready flow.

## ABI Version

- The active managed interop ABI is now `v3`.
- `ManagedInitArgs` now carries a native callback table so managed runtime code can proactively notify native code.

## Callback Bridge

The callback table currently includes:

- `ManagedNativeCallbacks.on_server_stub_ready`

The callback payload includes:

- native callback context
- assignment epoch
- managed-created ready entry data

## Runtime Flow

The service startup flow is now:

1. GM sends `Inner.ServerStubOwnershipSync (1202)` to Game.
2. Game forwards the ownership table into managed runtime state.
3. Managed runtime creates and activates owned stubs.
4. Each stub reaches `OnReady()` and immediately notifies native through the registered callback.
5. Native posts the ready event back onto the Game event loop thread, aggregates ready entries, and sends `Inner.GameServiceReadyReport (1203)` to GM after all locally owned stubs are ready.

## Compatibility Notes

- `GameNativeGetReadyServerStubCount` and `GameNativeGetReadyServerStubEntry` remain available for query-style tests and diagnostics.
- The authoritative startup path for ready propagation is now callback-driven rather than native snapshot polling.
