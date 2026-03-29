# Managed Interop V2 Update

Date: 2026-03-29

This note supplements `docs/MANAGED_INTEROP.md` with the ownership/ready bridge that is now implemented in code.

## ABI Version

- The active managed interop ABI is now `v2`.

## Additional Exports

In addition to `GameNativeGetAbiVersion`, `GameNativeInit`, `GameNativeOnMessage`, `GameNativeOnTick`, and the catalog exports, the native-to-managed runtime bridge now includes:

- `GameNativeApplyServerStubOwnership`
- `GameNativeResetServerStubOwnership`
- `GameNativeGetReadyServerStubCount`
- `GameNativeGetReadyServerStubEntry`

## Runtime Flow

The startup ownership flow is now:

1. GM sends `Inner.ServerStubOwnershipSync (1202)` to Game.
2. Game forwards the ownership table into managed runtime state.
3. Managed code creates the owned stub instances.
4. Each owned stub is activated and marked ready through `OnReady()`.
5. Native reads back the ready entries generated from the real managed stub instances.
6. Game sends `Inner.GameServiceReadyReport (1203)` to GM only after all locally owned stubs are ready.

## Scope Clarification

- GM still uses the catalog exports to discover server stub types.
- GM does not run the managed game loop.
- `GameNativeOnMessage` and `GameNativeOnTick` remain available for later runtime expansion, but the `M5-06` chain now already covers ownership application and ready reporting.
