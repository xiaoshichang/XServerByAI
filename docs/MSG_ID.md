# MSG_ID

本文档定义 XServerByAI 当前主线代码使用的 `msgId` 号段、命名约定与当前已登记消息。内容以当前仓库实现为准。

## 基础规则

1. `msgId` 使用 `uint32` 表示，`0` 保留为无效值。
2. 同一个消息一旦对外分配，不得静默改写语义；若发生不兼容变更，应分配新的 `msgId`。
3. 是否为响应由 `PacketHeader.flags.Response` 表达，不靠单独的新 `msgId` 命名。
4. 是否为错误由 `PacketHeader.flags.Error` 表达；错误原因见 [ERROR_CODE.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/ERROR_CODE.md)。

## 顶层号段

| Range | Category | Description |
| --- | --- | --- |
| `1-999` | 协议保留 | 公共协议语义与基础保留 |
| `1000-1999` | Inner 网络 | 注册、心跳、启动编排、ownership、ready |
| `2000-2099` | Gate/Game 运行期消息 | relay、avatar bootstrap、内部转发 |
| `2100-3999` | 预留 | 后续内部会话事件与扩展流量 |
| `4000-9999` | 客户端正式协议 | 为稳定客户端协议保留 |
| `10000-39999` | 预留 | 业务正式协议保留 |
| `40000-44999` | 运维 / 诊断 | 管理与诊断流量 |
| `45000-49999` | 测试 / 联调 | 当前本地调试、演示与临时客户端消息 |
| `50000+` | 预留 | 未来扩展 |

## 已登记 Inner 消息

| msgId | CanonicalName | Direction | Status | Description |
| --- | --- | --- | --- | --- |
| `1000` | `Inner.NodeRegister` | `Gate/Game -> GM`, `Game -> Gate` | `Active` | 注册请求；成功响应复用同一 `msgId` |
| `1100` | `Inner.NodeHeartbeat` | `Gate/Game -> GM`, `Game -> Gate` | `Active` | 心跳请求；成功响应复用同一 `msgId` |
| `1201` | `Inner.ClusterReadyNotify` | `GM -> Gate` | `Active` | 集群 ready 结论下发 |
| `1202` | `Inner.ServerStubOwnershipSync` | `GM -> Game` | `Active` | stub ownership 全量同步 |
| `1203` | `Inner.GameServiceReadyReport` | `Game -> GM` | `Active` | `Game` 本地 stub ready 上报 |
| `1204` | `Inner.ClusterNodesOnlineNotify` | `GM -> Game` | `Active` | 所有期望节点已上线通知 |
| `1205` | `Inner.GameGateMeshReadyReport` | `Game -> GM` | `Active` | `Game -> Gate` mesh ready 上报 |

## 已登记 Gate/Game 运行期消息

| msgId | CanonicalName | Direction | Status | Description |
| --- | --- | --- | --- | --- |
| `2000` | `Relay.ForwardToGame` | `Gate -> Game` | `Reserved` | 头文件中保留，当前主线未接通 |
| `2001` | `Relay.PushToClient` | `Game -> Gate` | `Active` | `Game` 通过 `Gate` 向客户端下行推送 |
| `2002` | `Relay.ForwardMailboxCall` | `Game -> Gate -> Game` | `Active` | mailbox / stub 调用经 `Gate` 转发 |
| `2003` | `Gate.CreateAvatarEntity` | `Gate -> Game` | `Active` | `Gate` 请求 `Game` 创建 avatar |
| `2004` | `Game.AvatarEntityCreateResult` | `Game -> Gate` | `Active` | `Game` 回传 avatar 创建结果 |
| `2005` | `Relay.ForwardProxyCall` | `Game -> Gate -> Game` | `Active` | proxy 调用经 `Gate` 二次寻址转发 |

## 已登记联调客户端消息

| msgId | CanonicalName | Direction | Status | Description |
| --- | --- | --- | --- | --- |
| `45010` | `Client.Hello` | `Client -> Gate` | `Active` | KCP 会话建立后的最小探活 / priming 消息 |
| `45011` | `Client.Move` | `Client -> Gate` | `Experimental` | 当前客户端模拟器默认占位消息号，主线 `Gate` 尚未处理 |
| `45012` | `Client.BuyWeapon` | `Client -> Gate` | `Experimental` | 当前客户端模拟器默认占位消息号，主线 `Gate` 尚未处理 |
| `45013` | `Client.SelectAvatar` | `Client <-> Gate` | `Active` | 选择 avatar；成功或失败响应复用同一 `msgId` |

## 命名约定

1. 规范英文名使用 `PascalCase` 片段并以 `.` 分隔。
2. 推荐格式：
   - `<Area>.<Action>`
   - `<Area>.<Subject>.<Action>`
3. 当前常用前缀包括：
   - `Inner`
   - `Relay`
   - `Gate`
   - `Game`
   - `Client`
4. 单向通知优先使用 `Notify`、`Report`、`Push` 等明确动词。

## 当前说明

1. `2001` / `2002` / `2005` 的结构定义见 [GATE_GAME_ENVELOPE.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/GATE_GAME_ENVELOPE.md)。
2. `2003` / `2004` 当前使用 JSON 负载完成 avatar 创建握手，不属于 `RelayCodec`。
3. `45000-49999` 仍然是测试 / 联调区；`45011`、`45012` 当前还不是稳定主线协议。
