# M3-04 测试记录

## 范围

- 分支：`M3-04`
- 开发提交：
  - `d086807`：`[developer] M3-04 implement GM register request handling and response`
- 设计依据：`docs/M3-04.md`
- 验证目标：确认 `GM` 能接住 `Inner.NodeRegister` 请求、正确写入注册表、映射错误码并返回稳定响应。

## 复测结果

- 依赖检查通过：`M2-12`、`M3-03` 已完成；复测时 `M3-04` 状态为 `开发中`
- Native 全量验证已执行并通过：
  - `cmake -S . -B build -DXS_BUILD_TESTS=ON`
  - `cmake --build build --config Debug`
  - `ctest --test-dir build -C Debug --output-on-failure`
  - 结果：当时记录的 native 用例全部通过
- 本次变更未触及 `XServerByAI.Managed.sln`、`src/managed/` 或其他 .NET 工程文件，因此未执行 managed build
- 自动化测试覆盖通过：
  - `xs_node_gm_register_tests` 验证 `Gate` / `Game` 注册成功写入注册表、成功响应的 `msgId` / `flags` / `seq` 正确、重复 `nodeId` 返回 `3001`、非法 `processType` 返回 `3000`、非法 `innerNetworkEndpoint` 返回 `3002`、非法 payload 返回 `3005`，以及损坏 packet 不污染注册表
  - `xs_node_gm_node_tests` 验证 `InnerNetwork` 监听、收包、路由回传与 GM 生命周期骨架没有被本条目回归破坏
  - `xs_net_register_codec_tests` 继续为注册 payload 的编解码稳定性提供底层保障
- 行为检查结果：
  - `InnerNetwork` 已能把 `routingId + payload` 交给 `GmNode`，支撑 ROUTER 链路上的注册处理
  - `GmNode` 能把注册成功请求写入 `ProcessRegistry`，并初始化 `lastHeartbeatAtUnixMs` 与默认心跳参数
  - 失败响应已与 `docs/PROCESS_INNER.md` / `docs/ERROR_CODE.md` 保持一致，不再需要测试侧自行猜测错误返回格式

## 最新结论

- `M3-04` 本轮测试通过。
- `docs/DEVELOPMENT_PLAN.md` 已将 `M3-04` 标记为 `已完成`。
- 后续 `M3-05` 可以直接在现有注册表与路由标识基础上承接心跳刷新、超时剔除与链路失效语义。
