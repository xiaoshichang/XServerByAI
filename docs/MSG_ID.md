# MSG_ID

本文档定义当前主线代码使用的 `msgId` 分段、命名约定与已登记消息，内容以仓库现状为准。

## 基础规则
1. `msgId` 使用 `uint32` 表示，`0` 保留为非法值。
2. 是否为响应由 `PacketHeader.flags.Response` 表达，不单独分配新的 `msgId`。
3. 是否为错误由 `PacketHeader.flags.Error` 表达。
4. 同一个已对外使用的 `msgId` 不应静默修改语义。

## 顶层分段
| Range | Category | Description |
| --- | --- | --- |
| `1-999` | 协议保留 | 公共基础协议保留 |
| `1000-1999` | Inner 网络 | 注册、心跳、启动编排、ownership、ready |
| `2000-2099` | Gate / Game 运行期消息 | relay、avatar bootstrap 等内部转发消息 |
| `2100-3999` | 预留 | 后续内部运行期扩展 |
| `4000-9999` | 稳定客户端协议 | 当前对客户端开放的稳定消息号段 |
| `45000-49999` | 联调 / 试验 | 本地演示与临时客户端消息 |
| `50000+` | 预留 | 后续扩展 |

## Inner 消息
| msgId | CanonicalName | Direction | Status | Description |
| --- | --- | --- | --- | --- |
| `1000` | `Inner.NodeRegister` | `Gate/Game -> GM`, `Game -> Gate` | `Active` | 注册请求；成功响应复用同一 `msgId` |
| `1100` | `Inner.NodeHeartbeat` | `Gate/Game -> GM`, `Game -> Gate` | `Active` | 心跳请求；成功响应复用同一 `msgId` |
| `1201` | `Inner.ClusterReadyNotify` | `GM -> Gate` | `Active` | 集群 ready 通知 |
| `1202` | `Inner.ServerStubOwnershipSync` | `GM -> Game` | `Active` | stub ownership 全量同步 |
| `1203` | `Inner.GameServiceReadyReport` | `Game -> GM` | `Active` | `Game` 本地服务 ready 上报 |
| `1204` | `Inner.ClusterNodesOnlineNotify` | `GM -> Game` | `Active` | 所有期望节点已上线通知 |
| `1205` | `Inner.GameGateMeshReadyReport` | `Game -> GM` | `Active` | `Game -> Gate` mesh ready 上报 |

## Gate / Game 运行期消息
| msgId | CanonicalName | Direction | Status | Description |
| --- | --- | --- | --- | --- |
| `2000` | `Relay.ForwardToGame` | `Gate -> Game` | `Reserved` | 头文件预留，当前主线未使用 |
| `2001` | `Relay.PushToClient` | `Game -> Gate` | `Active` | `Game` 通过 `Gate` 向客户端下行推送 |
| `2002` | `Relay.ForwardMailboxCall` | `Game -> Gate -> Game` | `Active` | mailbox / stub 调用经 `Gate` 转发 |
| `2003` | `Gate.CreateAvatarEntity` | `Gate -> Game` | `Active` | `Gate` 请求 `Game` 创建 AvatarEntity |
| `2004` | `Game.AvatarEntityCreateResult` | `Game -> Gate` | `Active` | `Game` 返回 AvatarEntity 创建结果 |
| `2005` | `Relay.ForwardProxyCall` | `Game -> Gate -> Game` | `Active` | proxy 调用经 `Gate` 二次寻址转发 |

## 客户端稳定消息
| msgId | CanonicalName | Direction | Status | Description |
| --- | --- | --- | --- | --- |
| `45010` | `Client.Hello` | `Client -> Gate` | `Active` | KCP 会话建立后的最小探活 / priming 消息 |
| `45013` | `Client.SelectAvatar` | `Client <-> Gate` | `Active` | 选择 Avatar；成功或失败响应复用同一 `msgId` |
| `6302` | `Client.EntityRpc` | `Client -> Gate -> Game` | `Active` | 客户端发起的 entity RPC，请求最终路由到当前 Avatar 所在 `Game` |
| `6303` | `Client.EntityRpcResult` | `Game -> Gate -> Client` | `Active` | `Game` 侧 `CallClientRpc(...)` 发送给客户端的 entity RPC |

## 联调 / 试验消息
| msgId | CanonicalName | Direction | Status | Description |
| --- | --- | --- | --- | --- |
| `45011` | `Client.Move` | `Client -> Gate` | `Experimental` | 当前模拟客户端默认占位消息号，主线 `Gate` 尚未处理 |
| `45012` | `Client.BuyWeapon` | `Client -> Gate` | `Experimental` | 当前模拟客户端默认占位消息号，主线 `Gate` 尚未处理 |

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

## 当前说明
1. `2001` / `2002` / `2005` 的结构见 `docs/GATE_GAME_ENVELOPE.md`。
2. `2003` / `2004` 当前使用 JSON 负载完成 AvatarEntity 创建握手。
3. `6302` / `6303` 使用 entity RPC JSON codec，当前首个业务样例为 `AvatarEntity.SetWeapon`。
