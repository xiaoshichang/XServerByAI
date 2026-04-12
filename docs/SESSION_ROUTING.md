# SESSION_ROUTING

本文档定义当前主线代码中 `Gate` 对客户端会话、账号、Avatar 与路由的管理方式，内容以 `ClientSession`、`GateNode::ClientSessionRecord` 与当前运行期逻辑为准。

## 接入流程
1. 客户端先调用 `Gate authNetwork` 的 `POST /login`。
2. `Gate` 为登录成功的账号创建带过期时间的 `conversation` 预约，并返回目标 KCP 地址。
3. 客户端使用该 `conversation` 建立 KCP 会话。
4. `Gate` 校验：
   - 集群已经 `clusterReady = true`
   - `conversation` 由当前 `Gate` 签发
   - 远端地址与登录来源一致
5. 接纳成功后，`Gate` 创建 `ClientSession` 与 `ClientSessionRecord`。
6. 客户端发送 `Client.Hello (45010)` 完成最小探活。
7. 客户端发送 `Client.SelectAvatar (45013)` 后，`Gate` 选择一个就绪 `Game`，发送 `2003 Gate.CreateAvatarEntity`，等待 `2004 Game.AvatarEntityCreateResult`。
8. AvatarEntity 创建成功后，`Gate` 绑定：
   - `sessionId -> avatarId`
   - `avatarId -> sessionId`
   - `avatarId -> gameNodeId`
9. 绑定完成后，客户端可以继续发送 `ClientToServerEntityRpc (6302)`；`Gate` 会把它重封装为 `Relay.ForwardProxyCall (2005)` 并路由到当前 avatar 所在的 `Game`。

## 关键结构

### ClientSessionRecord
| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `sessionId` | `uint64` | 对应 KCP 会话唯一标识 |
| `conversation` | `uint32` | 登录预约分配的会话号 |
| `accountId` | `string` | 当前认证账号 |
| `avatarId` | `string` | 当前已绑定 Avatar GUID，未绑定时为空 |
| `avatarName` | `string` | 当前 Avatar 显示名 |
| `gameNodeId` | `string` | 当前 Avatar 所在 `Game` 节点 |
| `gateNodeId` | `string` | 当前本地 `Gate` 节点 ID |
| `pendingSelectAvatarSeq` | `uint32` | 等待 `2004` 确认时暂存的客户端请求序号 |
| `authenticatedAtUnixMs` | `uint64` | 认证完成时间 |
| `lastActiveUnixMs` | `uint64` | 最近活跃时间 |
| `closed` | `bool` | 当前记录是否已关闭 |

### Gate 本地权威索引
1. `sessionId -> ClientSession`
2. `sessionId -> ClientSessionRecord`
3. `accountId -> sessionId`
4. `avatarId -> sessionId`

## 当前路由规则
1. 客户端只有在 `clusterReady = true` 后才能被接纳。
2. 新会话默认处于未绑定 Avatar 的状态。
3. `selectAvatar` 成功后，`sessionId`、`avatarId`、`gameNodeId` 形成稳定绑定。
4. `6302` 只能在当前会话已经绑定 Avatar 之后发送。
5. `Gate` 处理 `6302` 时会校验：
   - 包头 `flags == 0`
   - `seq != 0`
   - 会话记录存在且未关闭
   - 已绑定 `avatarId`
   - 已绑定 `gameNodeId`
   - `avatarId -> sessionId` 映射仍然命中当前会话
   - 目标 `Game` inner session 已连接、已注册、已 ready
6. 当前没有自动迁移；如果目标 `Game` 丢失，路由会视为不可用，等待更高层恢复策略。

## 与 Relay 的关系

### `Relay.PushToClient (2001)`
- `Gate` 使用 `targetEntityId` 查到 `avatarId -> sessionId`，再重新封装客户端包并发回当前 KCP 会话。

### `Relay.ForwardProxyCall (2005)`
- `Game -> Gate -> Game` 场景下，`Gate` 会校验 `routeGateNodeId` 与 `targetEntityId`，再根据当前 Avatar 绑定把消息继续转发到目标 `Game`。
- `Client -> Gate -> Game` 场景下，`Gate` 会把客户端的 `6302` 先转换成 `2005`，目标实体固定为当前会话绑定的 `avatarId`。

### `Relay.ForwardMailboxCall (2002)`
- mailbox 调用不依赖客户端会话，`Gate` 直接按 `targetGameNodeId` 转发到指定 `Game`。

## 当前边界
1. 当前路由模型只覆盖单 `Gate` 持有的本地会话目录。
2. 当前业务身份以 `avatarId` 为中心，而不是旧的 `playerId`。
3. `move` / `buyWeapon` 仍然是联调占位消息，不属于当前稳定路由链路。
