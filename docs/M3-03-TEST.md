# M3-03 测试记录

## 范围

- 分支：`M3-03`
- 开发提交：
  - `7677ae0`：`developer: add gm process registry`
- 设计依据：`docs/M3-03.md`
- 验证目标：确认 `ProcessRegistry` 能作为 GM 注册表基础设施稳定复用，具备 `nodeId` / `routingId` 查询、注销、负载更新、`innerNetworkReady` 更新与活动快照能力。

## 测试结果

- 依赖检查通过：`M1-09` 已完成，测试时 `M3-03` 状态为 `开发中`
- Native 全量验证已执行并通过：
  - `cmake -S . -B build -DXS_BUILD_TESTS=ON`
  - `cmake --build build --config Debug`
  - `ctest --test-dir build -C Debug --output-on-failure`
  - 结果：`14/14` 测试通过
- 本次开发提交未触及 `XServerByAI.Managed.sln`、`src/managed/` 或其他 .NET 工程文件，因此未执行 managed build
- 自动化测试覆盖通过：
  - `xs_node_process_registry_tests` 验证 Gate/Game 注册成功、`nodeId` 冲突、`routingId` 辅助索引查询与移除、负载更新、`innerNetworkReady` 更新以及活动快照输出
  - `xs_net_register_codec_tests` 与 `xs_net_heartbeat_codec_tests` 继续通过，说明注册与心跳共用结构在引入注册表后没有发生回归
- 行为与设计一致性检查通过：
  - `ProcessRegistryEntry` 已包含 `processType`、`nodeId`、`pid`、`startedAtUnixMs`、`innerNetworkEndpoint`、`buildVersion`、`capabilityTags`、`load`、`routingId`、`lastHeartbeatAtUnixMs` 与 `innerNetworkReady`
  - `ProcessRegistry` 以 `nodeId` 为主键、以 `routingId` 为辅助索引，并对 `processType`、`nodeId`、`innerNetworkEndpoint.host` 与 `innerNetworkEndpoint.port` 执行最小校验
  - `Snapshot()` 返回按 `nodeId` 稳定排序的结果，可直接支撑后续路由目录与 ready 聚合场景
- 本条目只交付数据结构与接口，不直接覆盖 ZeroMQ 消息收发；这与 `docs/M3-03.md` 的范围约束一致，因此未额外执行进程级 smoke 测试

## 最新结论

- `M3-03` 本轮测试通过。
- `docs/DEVELOPMENT_PLAN.md` 已将 `M3-03` 从 `开发中` 更新为 `已完成`。
- 后续 `M3-04`、`M3-05` 可以直接把注册与心跳请求映射到现有注册表接口上，不需要重复设计底层状态结构。
