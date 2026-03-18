# M2-14 测试记录

## 范围

- 分支：`M2-14`
- 开发提交：`d4427d7`（`[developer] Implement M2-14 listener metrics`）
- 设计依据：`docs/M2-14.md`

## 执行结果

- 依赖检查通过：`M2-08` 已完成，`M2-14` 当前仍为 `开发中`
- Native 全量验证已执行并通过：
  - `cmake -S . -B build -DXS_BUILD_TESTS=ON`
  - `cmake --build build --config Debug`
  - `ctest --test-dir build -C Debug --output-on-failure`
- 本次变更未触及 `XServerByAI.Managed.sln`、`src/managed/` 或其他 .NET 工程文件，因此未执行 managed build

## 问题清单

### 1. 阻断问题：空 payload 仍被当作业务消息处理

- 设计要求：`docs/M2-14.md` 明确要求 ZeroMQ monitor 事件与空 payload 通知都不能进入 `SetMessageHandler()`，也不能计入吞吐统计。
- 当前实现：`ZmqPassiveListener::PollIncomingMessages()` 在 `ReceiveMessage()` 成功后会无条件执行 `metrics_.RecordReceivedMessage(message.size())`，并继续调用 `message_handler_`。
- 影响：
  - zero-length payload 会错误增加 `receivedMessageCount`
  - 空 payload 虽然不会增加 `receivedPayloadBytes`，但仍会被计入“收到一条业务消息”
  - 业务层会收到本应被基础层过滤掉的空通知
- 结论：当前实现不满足 `M2-14` 对 metrics 更新边界与业务消息过滤边界的要求。

### 2. 重要问题：自动化测试缺少空 payload 回归用例

- 当前 `xs_ipc_passive_listener_tests` 已覆盖：
  - 初始快照全零
  - 正常业务消息收发后的计数与字节累计
  - `Stop()` 后快照保留
  - `Start()` 后统计重置
  - monitor 驱动的连接数增减
  - multipart payload 拒绝
- 当前缺口：
  - 没有验证 empty payload 必须被忽略
  - 没有验证 empty payload 不得进入 `SetMessageHandler()`
  - 没有验证 empty payload 不得增加 `receivedMessageCount`
- 影响：上述阻断问题当前无法被 CI 自动发现。

## 结论

- `M2-14` 本轮测试未通过。
- `docs/DEVELOPMENT_PLAN.md` 保持 `开发中` 不变，不应提交 `[Feature Passed]` 状态变更。
- 需要先修复空 payload 过滤逻辑，并补充对应回归测试，再重新执行 native 全量验证。
