# M3-04 测试记录

## 范围

- 分支：`M3-04`
- 开发提交：
  - `d086807`：`[developer] M3-04 implement GM register request handling and response`
- 设计依据：`docs/M3-04.md`
- 验证目标：确认 GM 控制面已接管 `Control.ProcessRegister`，能够在主事件循环内完成注册请求解码、注册表写入、错误映射与响应发送，并满足 `docs/PROCESS_CONTROL.md` / `docs/ERROR_CODE.md` 约定的协议与错误语义。

## 测试结果

- 依赖检查通过：`M2-12`、`M3-03` 已完成，测试前 `M3-04` 状态为 `开发中`
- Native 全量验证已执行并通过：
  - `cmake -S . -B build -DXS_BUILD_TESTS=ON`
  - `cmake --build build --config Debug`
  - `ctest --test-dir build -C Debug --output-on-failure`
  - 结果：`15/15` 测试通过
- 本次开发提交未触及 `XServerByAI.Managed.sln`、`src/managed/` 或其他 .NET 工程文件，因此未执行 managed build
- 自动化测试覆盖通过：
  - `xs_node_gm_register_tests` 端到端验证 `Game` 注册成功并写入注册表、`Gate` / `Game` 在同一 GM 监听端口上连续注册成功、成功响应的 `msgId` / `flags` / `seq` 回显、重复 `nodeId` 返回 `3001`、非法 `processType` 返回 `3000`、非法 `serviceEndpoint` 返回 `3002`、保留字段非法返回 `3005`，以及损坏外层包不会污染注册表且不会产生响应
  - `xs_node_gm_node_tests` 继续通过，说明 `InnerNetwork` 的监听生命周期、路由消息接收与事件循环内运行没有被本条目回归破坏
  - `xs_net_register_codec_tests` 与其余 native 用例继续通过，说明本条目对注册响应与控制面错误路径的接入没有破坏既有编解码与基础模块
- 代码与设计一致性检查通过：
  - `InnerNetwork` 已暴露稳定的“收到路由消息”回调与“按 routingId 回包”能力，仍保持只负责 ROUTER 传输语义
  - `GmNode` 仅接管 `msgId = 1000` 且 `Response` / `Error` 标志未置位、`seq != 0` 的注册请求；无法形成稳定协议上下文的损坏包记录日志后丢弃
  - 注册成功时会把 `routingId`、`lastHeartbeatAtUnixMs`、`serviceReady = false` 写入 `ProcessRegistry`，并返回默认 `5000ms / 15000ms` 心跳参数与 `serverNowUnixMs`
  - 注册失败时错误码与文档一致：`3000 Control.ProcessTypeInvalid`、`3001 Control.NodeIdConflict`、`3002 Control.ServiceEndpointInvalid`、`3005 Control.RegisterPayloadInvalid`

## 最新结论

- `M3-04` 本轮测试通过。
- `docs/DEVELOPMENT_PLAN.md` 已将 `M3-04` 从 `开发中` 更新为 `已完成`。
- 后续 `M3-05`、`M3-06`、`M3-12` 可以直接复用本条目形成的注册表写入结果、错误响应语义与控制链路响应能力。
