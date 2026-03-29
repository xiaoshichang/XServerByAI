# M5-06 Implementation Update

Date: 2026-03-29

This note records the implementation status that now exists in code for `M5-06`.

## Delivered

- The managed runtime type is now `GameNodeRuntimeState`.
- The old `GameRuntime` name is removed from the managed runtime implementation and tests.
- `ServerStubReadyState` is removed.
- Stub readiness now comes directly from each owned `ServerStubEntity.IsReady` flag.
- `GameNativeInit` now creates the managed runtime state for the current Game node.
- Native `GameNode` now loads and binds the managed runtime during `OnInit`.
- `GameNode::ApplyStubOwnership()` no longer fabricates local GUIDs for ready reporting.
- Native now forwards the `1202` ownership sync into managed code, receives managed-created ready notifications through a callback bridge, and then sends `1203` to GM.
- `GameNode` now reports service ready only after all locally owned stubs are ready for the current assignment epoch.
- Managed `ServerStubEntity.OnReady()` now feeds a reusable managed-to-native callback path, so later asynchronous stub initialization can reuse the same bridge.

## ABI Additions

The ownership and ready bridge now includes:

- `GameNativeApplyServerStubOwnership`
- `GameNativeResetServerStubOwnership`
- `GameNativeGetReadyServerStubCount`
- `GameNativeGetReadyServerStubEntry`
- `ManagedInitArgs.native_callbacks`
- `ManagedNativeCallbacks.on_server_stub_ready`

The catalog exports remain unchanged in purpose:

- `GameNativeGetServerStubCatalogCount`
- `GameNativeGetServerStubCatalogEntry`

## Verification

Verified on 2026-03-29 with:

- `dotnet build XServerByAI.Managed.sln --no-restore -m:1`
- `dotnet test src/managed/Tests/Framework.Tests/Framework.Tests.csproj --no-build`
- `xs_host_runtime_tests`
- `xs_node_game_gm_session_tests`
- `xs_node_game_node_tests`
