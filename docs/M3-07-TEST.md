# M3-07 测试记录

## 范围

- 分支：`codex/M3-07`
- 设计依据：`docs/M3-07.md`
- 验证目标：确认 `ClusterConfig` / `NodeConfig` 收敛、`Inner` / `Control` / `Client` 命名规范、样例配置以及 Gate 客户端网络占位骨架都已按新模型生效。

## 测试结果

- Native 全量验证已执行并通过：
  - `cmake -S . -B build -DXS_BUILD_TESTS=ON`
  - `cmake --build build --config Debug`
  - `ctest --test-dir build -C Debug --output-on-failure`
- 自动化覆盖包括：
  - `xs_core_json_tests`：验证 `ClusterConfig` 加载、`NodeConfig` 选择、`gate.clientNetwork` 必填、非法 `NodeID` 拒绝与样例 schema 一致性
  - `xs_node_create_helper_tests`：验证统一入口、节点角色分派与基于 `NodeConfig` 派生类型的节点选择
  - `xs_node_gate_node_tests`：验证 `GateNode` 已消费 `clientNetwork.listenEndpoint` 与集群级 `kcp`，并将其接入 `ClientNetwork` 占位骨架
  - 既有 `GM` / 注册 / 心跳 / 注册表测试全部继续通过，说明本次配置收敛未破坏现有 `Inner` 运行时路径

## 结论

- `ClusterConfig` 已成为统一配置事实来源。
- `NodeConfig` 已精简为角色专属字段，不再冗余保存历史节点身份字段。
- `configs/local-dev.json` 可作为当前开发基线样例配置。
- `GateNode` 已具备稳定的 `ClientNetwork` 配置消费边界，可供后续 `M4-02` 继续实现真实客户端监听。
