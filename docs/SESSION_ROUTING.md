# SESSION_ROUTING

本文档定义 XServerByAI 当前阶段 `Gate` 的会话与路由数据模型。它描述客户端会话、`Gate` 本地路由目录、`Game` 目标绑定以及与分布式实体寻址的关系。实体 ownership、`Mailbox` / `Proxy` 语义仍以 `docs/DISTRIBUTED_ENTITY.md` 为准。

**适用范围**
1. 当前模型覆盖 `Gate` 维护的客户端逻辑会话、`Gate` 视角的 `Game` 路由目录，以及后续 `Gate -> Game -> C#` 转发链路。
2. 当前模型假定 `Game` 通过 `Inner.NodeRegister` / `Inner.NodeHeartbeat` 主动向每个 `Gate` 完成注册与心跳；`Gate` 本地路由目录来自这些闭环结果，而不是 `GM` 主动推送的目录快照。
3. `Gate` 只有在收到 `GM` 下发的 `clusterReady = true` 后，才允许真正接纳客户端会话；但它可以在此之前提前维护 `Game` 目录与内部转发目标。

**基础约定**
1. `sessionId` 使用 `uint64`，`0` 为无效值；同一 `Gate` 生命周期内不得复用已分配 `sessionId`。
2. `playerId` 使用 `uint64`，`0` 表示尚未完成玩家绑定或当前阶段未知。
3. `gameNodeId` 与 `gateNodeId` 都取自对应节点注册时上报的稳定 `NodeID`。
4. 目录中的 `innerNetworkEndpoint` 表示 `Gate` 对应 `Game` 的内部转发目标地址。
5. 所有时间戳字段统一使用 `unixTimeMsUtc` 语义；未知或未发生时为 `0`。

**枚举：SessionState**

| Value | Name | Description |
| --- | --- | --- |
| `1` | `Created` | `Gate` 已创建逻辑会话，但尚未完成鉴权 |
| `2` | `Authenticating` | 会话正在鉴权或加载玩家上下文 |
| `3` | `Active` | 会话可以收发正常业务消息 |
| `4` | `Closing` | 会话正在执行关闭流程 |
| `5` | `Closed` | 会话已关闭 |

**枚举：RouteState**

| Value | Name | Description |
| --- | --- | --- |
| `1` | `Unassigned` | 当前会话尚未绑定目标 `Game` |
| `2` | `Selecting` | `Gate` 正在从可用目录中选择目标 `Game` |
| `3` | `Bound` | 当前会话已经固定绑定到某个 `Game` |
| `4` | `RouteLost` | 已绑定 `Game` 失效、掉线或不再可转发 |
| `5` | `Released` | 会话关闭后，路由关系已经释放 |

**枚举：GameRouteState**

| Value | Name | Description |
| --- | --- | --- |
| `1` | `Available` | 可以接收新的会话绑定与业务转发 |
| `2` | `Draining` | 不再接收新会话，但保留已有绑定会话完成收尾 |
| `3` | `Unavailable` | 当前不可作为转发目标 |

**结构：RouteTarget**

| Field | Type | Description |
| --- | --- | --- |
| `gameNodeId` | `string` | 当前绑定目标 `Game` 的稳定 `NodeID` |
| `innerNetworkEndpoint` | `Endpoint` | 当前 `Gate` 向该 `Game` 转发时使用的地址 |
| `routeEpoch` | `uint64` | 当前目录版本号 |

`RouteTarget` 只表达“当前会话绑定到了哪个 `Game`”，不表达业务实体 ownership 本身。

**结构：SessionRecord**

| Field | Type | Description |
| --- | --- | --- |
| `sessionId` | `uint64` | `Gate` 内唯一的逻辑会话标识 |
| `gateNodeId` | `string` | 当前承载该会话的 `Gate` `NodeID` |
| `sessionState` | `uint16` | 取值见 `SessionState` |
| `playerId` | `uint64` | 当前绑定玩家；未绑定时为 `0` |
| `routeState` | `uint16` | 取值见 `RouteState` |
| `routeTarget` | `RouteTarget` | 当前绑定的目标 `Game`；未绑定时视为无效 |
| `connectedAtUnixMs` | `uint64` | 会话创建时间 |
| `authenticatedAtUnixMs` | `uint64` | 完成鉴权时间；未发生时为 `0` |
| `lastActiveUnixMs` | `uint64` | 最近一次业务活动时间 |
| `closedAtUnixMs` | `uint64` | 会话关闭时间；未关闭时为 `0` |
| `closeReasonCode` | `int32` | 会话关闭原因；未关闭时为 `0` |

**结构：GameDirectoryEntry**

| Field | Type | Description |
| --- | --- | --- |
| `gameNodeId` | `string` | `Game` 的稳定 `NodeID` |
| `routeState` | `uint16` | 取值见 `GameRouteState` |
| `innerNetworkEndpoint` | `Endpoint` | 当前 `Gate` 对该 `Game` 的内部转发目标地址 |
| `capabilityTags` | `string[]` | 该 `Game` 注册到当前 `Gate` 时声明的能力标签 |
| `load` | `LoadSnapshot` | 最近一次心跳带来的负载快照 |
| `routeEpoch` | `uint64` | 当前 `Gate` 本地目录版本 |
| `updatedAtUnixMs` | `uint64` | 最近一次刷新目录项的时间 |

`GameDirectoryEntry` 的来源是“`Game` 对当前 `Gate` 的注册与心跳会话”，不是 `GM` 主动推送的目录快照。不同 `Gate` 的目录版本由各自本地维护，但都必须建立在同一套 `Game -> Gate` 注册事实之上。

**Gate 本地权威表**
1. `sessionId -> SessionRecord` 是 `Gate` 内部会话状态的权威表。
2. `playerId -> sessionId[]` 用于按玩家快速定位会话。
3. `gameNodeId -> sessionId[]` 用于在 `Game` 路由失效时快速找到受影响会话。
4. `gameNodeId -> GameDirectoryEntry` 是 `Gate` 的本地 `Game` 路由目录。

**路由规则**
1. `Gate` 只有在本地 `GameDirectoryEntry.routeState = Available` 且自身已经收到 `clusterReady = true` 后，才允许把新客户端会话绑定到该 `Game`。
2. 默认采用“首次选择后固定绑定”的策略：一旦 `SessionRecord.routeState = Bound`，同一 `sessionId` 不会自动迁移到新的 `Game`。
3. 当某个 `Game` 到当前 `Gate` 的注册会话断开、心跳超时或被明确标记为不可用时，对应 `GameDirectoryEntry` 必须切换到 `Unavailable` 或 `Draining`，并据此更新受影响会话。
4. `RouteLost` 只表达当前会话的原始目标已经不再可转发，不等价于自动迁移、自动重登或玩家踢下线；后续如何处理由更高层流程决定。
5. `Gate` 可以在 `clusterReady = false` 时预热与维护目录，但不得提前接受客户端入口，也不得把本地目录可用性当作替代 `clusterReady` 的结论。

**与 Gate↔Game 封装的关系**
1. `RelayEnvelopeHeader.sessionId` 与 `RelayEnvelopeHeader.playerId` 应直接复用 `SessionRecord` 语义。
2. `Gate -> Game` 转发时必须校验 `SessionRecord.routeTarget` 是否仍然匹配当前目录中的可用项。
3. `Game` 处理 `Relay.ForwardToGame` 时，应验证自己是否仍是该会话的预期目标；否则应返回路由失配错误，而不是静默消费。

**与分布式实体的关系**
1. `AvatarEntity` 这类可迁移实体通过 `Proxy` 间接依赖 `SessionRecord` 与当前路由目录。
2. `SpaceEntity`、`ServerStubEntity` 这类静态寻址实体通过 `Mailbox` 直接引用目标 `GameNodeId`，仍然由 `Gate` 根据本地目录完成转发。
3. 会话路由与实体 ownership 是两套相关但不等价的状态：会话绑定的是“当前客户端入口该发往哪个 `Game`”，实体 ownership 表达的是“哪个 `Game` 持有该逻辑实体的权威活动实例”。

**后续条目约束**
1. `M4-05` 应围绕 `sessionId -> SessionRecord` 建立权威会话表。
2. `M4-06` 应基于“`Game` 已注册到当前 `Gate`”这一事实实现请求转发链路，而不是再引入一套独立的 GM 下发目录模型。
3. `M4-10` 的会话断开清理应依赖 `gameNodeId -> sessionId[]` 索引，保证 `Game` 路由失效时能快速定位受影响会话。
4. `M4-11` 的固定绑定策略只允许决定“如何选择首次目标 `Game`”，不应改写 `RouteTarget` 与 `RouteState` 的基础语义。
5. `M4-12` 的负载感知分配应读取 `GameDirectoryEntry.load`，并只在 `Available` 的目录项中选择候选目标。

