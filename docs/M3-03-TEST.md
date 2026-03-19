# M3-03 测试记录

## 范围

- 分支：`M3-03`
- 开发提交：
  - `7677ae0`：`developer: add gm process registry`
- 设计依据：`docs/M3-03.md`
- 验证目标：确认 `ProcessRegistry` 已作为独立 GM 注册表模块落地，具备稳定的条目结构、按 `nodeId` / `routingId` 查询与移除、心跳/负载更新、`serviceReady` 更新，以及确定性的活动快照导出能力。

## 测试结果

- 依赖检查通过：`M1-09` 已完成，测试前 `M3-03` 状态为 `开发中`
- Native 全量验证已执行并通过：
  - `cmake -S . -B build -DXS_BUILD_TESTS=ON`
  - `cmake --build build --config Debug`
  - `ctest --test-dir build -C Debug --output-on-failure`
  - 结果：`14/14` 测试通过
- 本次开发提交未触及 `XServerByAI.Managed.sln`、`src/managed/` 或其他 .NET 工程文件，因此未执行 managed build
- 自动化测试覆盖通过：
  - `xs_node_process_registry_tests` 验证 Gate/Game 注册成功、`nodeId` / `routingId` 冲突、按主键与链路索引查询、移除后索引回收、心跳与负载更新、`serviceReady` 更新、活动快照排序与空表场景
  - `xs_net_register_codec_tests` 与 `xs_net_heartbeat_codec_tests` 继续通过，说明本次对控制面共享结构与编解码调整未引入回归
- 代码与设计一致性检查通过：
  - `ProcessRegistryEntry` 已包含 `processType`、`nodeId`、`pid`、`startedAtUnixMs`、`serviceEndpoint`、`buildVersion`、`capabilityTags`、`load`、`routingId`、`lastHeartbeatAtUnixMs`、`serviceReady`
  - `ProcessRegistry` 使用 `nodeId` 作为主索引，使用 `routingId` 作为辅助索引，并对 `processType`、`nodeId`、`serviceEndpoint.host`、`serviceEndpoint.port` 执行最小语义校验
  - `Snapshot()` 通过有序容器导出稳定的 `nodeId` 排序结果，符合后续路由目录与 ready 聚合的复用预期
- 本条目只交付数据结构与接口层能力，没有直接接入 ZeroMQ 消息收发；这一点与 `docs/M3-03.md` 的范围约束一致，因此未额外执行可执行程序 smoke 用例

## 最新结论

- `M3-03` 本轮测试通过。
- `docs/DEVELOPMENT_PLAN.md` 已将 `M3-03` 从 `开发中` 更新为 `已完成`。
- 后续 `M3-04`、`M3-05` 可以直接将注册请求与心跳请求映射到该注册表接口，而无需重写容器语义。