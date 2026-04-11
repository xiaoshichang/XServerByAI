# SESSION_ROUTING

本文档定义 XServerByAI 当前主线代码里 `Gate` 的会话、认证、avatar 绑定与路由模型。内容以 `ClientSession`、`GateNode::ClientSessionRecord` 与当前 `Gate` 运行期逻辑为准。

## 适用范围

1. 适用于 `Gate` 的 HTTP 登录预约、KCP 会话接入与会话状态机。
2. 适用于 `Gate` 侧 avatar 绑定后的路由选择与目录解析。
3. 适用于 `Relay.PushToClient`、`Relay.ForwardProxyCall` 与 `Relay.ForwardMailboxCall` 的路由落点规则。

## 当前接入流程

1. 客户端先调用 `authNetwork` 的 `POST /login`。
2. `Gate` 为登录成功的账号创建一个 30 秒有效的 `conversation` 预约，并返回目标 KCP 端点。
3. 客户端使用该 `conversation` 打开 KCP 会话。
4. `Gate` 会校验：
   - 当前集群已经 `clusterReady = true`
   - `conversation` 是本 `Gate` 签发的
   - 远端地址与 HTTP 登录来源一致
5. 会话通过后，`Gate` 创建 `ClientSession` 与 `ClientSessionRecord`。
6. 客户端发送 `Client.Hello (45010)` 完成最小探活。
7. 客户端发送 `Client.SelectAvatar (45013)` 后，`Gate` 选择一个就绪 `Game`，发出 `2003 Gate.CreateAvatarEntity`，等待 `2004 Game.AvatarEntityCreateResult`。
8. 确认成功后，`Gate` 才把该会话绑定到具体 avatar 与 `Game` 路由。

## 当前核心结构

### `ClientSessionState`

| Value | Name | Description |
| --- | --- | --- |
| `1` | `Created` | 会话对象已创建 |
| `2` | `Authenticating` | 正在进入认证/接纳流程 |
| `3` | `Active` | 会话已可正常收发客户端数据 |
| `4` | `Closing` | 正在关闭 |
| `5` | `Closed` | 已关闭 |

### `ClientRouteState`

| Value | Name | Description |
| --- | --- | --- |
| `1` | `Unassigned` | 尚未绑定目标 `Game` |
| `2` | `Selecting` | 正在等待 `Game` 确认 avatar 创建 |
| `3` | `Bound` | 已绑定目标 `Game` |
| `4` | `RouteLost` | 原路由失效 |
| `5` | `Released` | 会话关闭后路由已释放 |

### `ClientRouteTarget`

| Field | Type | Description |
| --- | --- | --- |
| `gameNodeId` | `string` | 当前会话绑定的目标 `Game` `NodeID` |
| `innerNetworkEndpoint` | `Endpoint` | 当前 `Gate -> Game` 转发使用的内部地址 |
| `routeEpoch` | `uint64` | 当前路由版本；当前实现使用 `clusterReadyEpoch`，未收到时回退到 `1` |

### `ClientSession`

`ClientSession` 是 KCP 连接本身的权威状态，当前快照字段包括：

| Field | Type | Description |
| --- | --- | --- |
| `gateNodeId` | `string` | 当前承载该会话的 `Gate` |
| `sessionId` | `uint64` | `Gate` 内唯一会话标识，`0` 非法 |
| `conversation` | `uint32` | HTTP 登录签发的 KCP conversation |
| `remoteEndpoint` | `Endpoint` | 客户端远端地址 |
| `sessionState` | `ClientSessionState` | 会话状态 |
| `routeState` | `ClientRouteState` | 路由状态 |
| `routeTarget` | `optional<ClientRouteTarget>` | 绑定后有效 |
| `connectedAtUnixMs` | `uint64` | 建连时间 |
| `authenticatedAtUnixMs` | `uint64` | 通过接纳时间 |
| `lastActiveUnixMs` | `uint64` | 最近活动时间 |
| `closedAtUnixMs` | `uint64` | 关闭时间 |
| `closeReasonCode` | `int32` | 关闭原因 |

### `ClientSessionRecord`

`ClientSessionRecord` 是 `GateNode` 维护的业务侧扩展信息：

| Field | Type | Description |
| --- | --- | --- |
| `sessionId` | `uint64` | 对应 `ClientSession.sessionId` |
| `conversation` | `uint32` | 对应登录预约的 conversation |
| `accountId` | `string` | 当前已认证账号 |
| `avatarId` | `string` | 当前已绑定 avatar GUID；未绑定时为空 |
| `avatarName` | `string` | 当前 avatar 显示名 |
| `gameNodeId` | `string` | 当前 avatar 所属目标 `Game` |
| `gateNodeId` | `string` | 当前本地 `Gate` `NodeID` |
| `pendingSelectAvatarSeq` | `uint32` | 等待 `2004` 回包时暂存的客户端请求序号 |
| `authenticatedAtUnixMs` | `uint64` | 认证通过时间 |
| `lastActiveUnixMs` | `uint64` | 最近业务活动时间 |
| `closed` | `bool` | 当前记录是否已关闭 |

## Gate 本地权威索引

当前 `Gate` 维护以下权威表：

1. `sessionId -> ClientSession`
2. `sessionId -> ClientSessionRecord`
3. `accountId -> sessionId`
4. `avatarId -> sessionId`

说明：

1. 当前主线路由模型围绕 `accountId` 与 `avatarId` 展开，不再把 `playerId` 作为 Gate 侧权威字段。
2. `avatarId -> sessionId` 是 proxy 与 push 链路的关键索引。

## 路由目录来源

1. `Gate` 的 `Game` 目录来自 `Game -> Gate` 的注册与心跳闭环。
2. 只有注册完成、链路在线、`inner_network_ready = true` 的 `Game` 才会被视为可路由目标。
3. 当前选择 avatar 所用的 `ResolveAvatarGameNodeId()` 会在全部可用 `Game` 中随机选择一个。

## 当前路由规则

1. 客户端会话只有在 `clusterReady = true` 后才能被接纳。
2. 新会话最初处于 `RouteState = Unassigned`。
3. 当 `Gate` 发送 `2003 Gate.CreateAvatarEntity` 后，会把会话推进到 `Selecting` 流程。
4. 收到 `2004 Game.AvatarEntityCreateResult` 成功回包后：
   - `ClientSession.routeState` 变为 `Bound`
   - `routeTarget.gameNodeId` 指向确认创建该 avatar 的 `Game`
   - `ClientSessionRecord.avatarId` 与 `gameNodeId` 成为运行期权威值
5. 如果等待中的 avatar 创建失败，`Gate` 会回滚 `avatarId` / `gameNodeId` / `pendingSelectAvatarSeq`，并向客户端返回失败响应。
6. 当前没有自动迁移。目标 `Game` 失效后，会话只会进入 `RouteLost` 或等待更高层重新选择。

## 与 relay 的关系

### `Relay.PushToClient (2001)`

1. `Gate` 使用 `targetEntityId` 在 `avatarId -> sessionId` 中查找会话。
2. 找到会话后，重新封装客户端包并发给对应 KCP 会话。

### `Relay.ForwardProxyCall (2005)`

1. `Gate` 先校验 `routeGateNodeId` 必须等于当前本地 `Gate`。
2. 再通过 `targetEntityId` 命中 avatar 会话。
3. 然后读取 `ClientSessionRecord.gameNodeId`，把消息继续转发到当前承载该 avatar 的 `Game`。

### `Relay.ForwardMailboxCall (2002)`

1. mailbox 调用不依赖客户端会话。
2. `Gate` 直接按 `targetGameNodeId` 转发给目标 `Game`。

## 当前边界

1. 当前路由模型只覆盖单 `Gate` 持有的本地会话目录，不覆盖跨 `Gate` 会话迁移。
2. 当前会话绑定完成的关键业务身份是 `avatarId`，不是旧设计里的 `playerId`。
3. `move` / `buyWeapon` 等客户端消息号仍处于联调阶段，尚未进入统一稳定转发链路。
