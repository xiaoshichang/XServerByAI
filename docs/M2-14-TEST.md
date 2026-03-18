# M2-14 测试记录

## 范围

- 分支：`M2-14`
- 开发提交：
  - `d4427d7`（`[developer] Implement M2-14 listener metrics`）
  - `2539bdb`（`[developer] Fix M2-14 empty payload filtering`）
- 设计依据：`docs/M2-14.md`

## 初测问题

### 1. 已修复：空 payload 曾被当作业务消息处理

- 初测时发现 `ZmqPassiveListener::PollIncomingMessages()` 在 `ReceiveMessage()` 成功后会无条件执行 `metrics_.RecordReceivedMessage(message.size())`，并继续调用 `message_handler_`。
- 这会导致 zero-length payload 被错误计入 `receivedMessageCount`，也会让业务层收到本应由基础层过滤掉的空通知。
- 开发提交 `2539bdb` 已在进入 metrics 与 handler 前显式忽略空 payload。

### 2. 已修复：自动化测试曾缺少空 payload 回归用例

- 初测时 `xs_ipc_passive_listener_tests` 缺少 “empty payload 必须被忽略” 的验证，因此上述问题无法被 CI 自动发现。
- 开发提交 `2539bdb` 已新增 `TestListenerIgnoresEmptyPayloadMessages()`，覆盖：
  - empty payload 不进入 `SetMessageHandler()`
  - empty payload 不增加 `receivedMessageCount`
  - empty payload 不增加 `receivedPayloadBytes`
  - 后续合法消息仍可正常处理

## 复测结果

- 依赖检查通过：`M2-08` 已完成，`M2-14` 复测前状态为 `开发中`
- Native 全量验证已执行并通过：
  - `cmake -S . -B build -DXS_BUILD_TESTS=ON`
  - `cmake --build build --config Debug`
  - `ctest --test-dir build -C Debug --output-on-failure`
- 本次变更未触及 `XServerByAI.Managed.sln`、`src/managed/` 或其他 .NET 工程文件，因此未执行 managed build
- `xs_ipc_passive_listener_tests` 通过，说明以下设计要求已得到覆盖并通过验证：
  - 初始快照全零
  - 业务收发后消息数与字节数累计正确
  - `Start()` 后历史统计重置
  - `Stop()` 后快照仍可读取
  - monitor 驱动的连接数随 connect/disconnect 正确增减
  - monitor 事件不会伪造成业务消息
  - empty payload 不进入业务 handler，也不计入吞吐统计

## 最新结论

- `M2-14` 本轮复测通过。
- `docs/DEVELOPMENT_PLAN.md` 应将 `M2-14` 从 `开发中` 更新为 `已完成`。
