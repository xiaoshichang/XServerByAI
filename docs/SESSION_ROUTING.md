# SESSION_ROUTING

本文档定义 XServerByAI 当前阶段 Gate 侧客户端会话、Gate 的传输路由绑定以及 GM 提供的 Game 节点路由目录的数据模型。后续 `M4-04`、`M4-10`、`M4-11`、`M4-12` 与 `M5-08` 在实现会话表、断线清理、固定绑定路由、负载感知分配与实体路由时，应以本文件作为字段语义与状态约束的基线；实体侧的 ownership、迁移属性与 `Mailbox` / `Proxy` 语义见 `docs/DISTRIBUTED_ENTITY.md`。

**适用范围**
1. 当前覆盖 Gate 本地维护的客户端会话记录、会话到 Game 的绑定关系，以及供 Gate 选择目标 Game 的目录条目。
2. 当前模型服务于 `KCP session -> Gate logical session -> transport route target -> C# entity route hint` 这一链路，不直接承载鉴权协议细节或场景/实体业务状态；具体实体分类、迁移属性与路由终点语义由 `docs/DISTRIBUTED_ENTITY.md` 统一定义。
3. 当前阶段不定义跨 Gate 的共享会话、不定义会话迁移、不定义 Game 间动态负载迁移；这些能力如需引入，应在兼容本模型的前提下扩展。
4. 当前默认每个服务器组固定 `1` 个 GM，管理同组内 `N` 个 Gate 节点与 `M` 个 Game 节点，其中 `N >= 1`、`M >= 1`；Gate 与 Game 采用全连接拓扑，Game↔Game 与 Gate↔Gate 不直接连接。

**共享约定**
1. 若本模型中的字段被序列化到内部协议消息体，应沿用内部协议统一约定，使用网络字节序（大端）编码；`string`、`string[]`、`Endpoint` 与 `LoadSnapshot` 的编码规则复用 `docs/PROCESS_CONTROL.md`。
2. `sessionId` 使用 `uint64` 语义，`0` 为无效值；同一个 Gate 进程生命周期内不得复用已分配的 `sessionId`。
3. `playerId` 使用 `uint64` 语义，`0` 表示“尚未绑定玩家”或“该阶段未知”，不得与合法玩家编号冲突。
4. `gameNodeId` 与 `gateNodeId` 复用 `Control.ProcessRegister.nodeId`，分别表示 Game/Gate 节点的稳定逻辑身份；`gameRegistrationId` 复用注册成功响应中的 `registrationId`，表示当前有效注册租约。
5. 所有时间戳字段均使用 `unixTimeMsUtc` 语义；未知或尚未发生的时间点统一填 `0`。
6. 会话重连在当前阶段视为“创建一个新会话”；旧 `sessionId` 不得被复活或转移给新的网络连接。
7. `NodeID` 使用区分大小写的格式 `<ProcessType><index>`，例如 `Gate0`、`Gate1`、`Game0`、`Game1`；同一服务器组内不得出现重复 `NodeID`。

**枚举：SessionState**

| 值 | 名称 | 说明 |
| --- | --- | --- |
| `1` | `Created` | Gate 已创建逻辑会话，但尚未完成鉴权或业务绑定 |
| `2` | `Authenticating` | 正在执行鉴权、账号校验或玩家装载等前置流程 |
| `3` | `Active` | 会话可收发业务消息，允许参与正常路由 |
| `4` | `Closing` | 已进入清理流程，不再接受新的业务请求 |
| `5` | `Closed` | 会话已关闭，仅保留诊断或延迟回收所需信息 |

**枚举：RouteState**

| 值 | 名称 | 说明 |
| --- | --- | --- |
| `1` | `Unassigned` | 尚未分配目标 Game |
| `2` | `Selecting` | Gate 正在根据当前目录挑选目标 Game |
| `3` | `Bound` | 已固定绑定到单个 Game，后续业务请求应持续命中同一目标 |
| `4` | `RouteLost` | 先前绑定的 Game 已不可用、租约失效或连接失效 |
| `5` | `Released` | 会话已结束，路由关系已释放 |

**枚举：GameRouteState**

| 值 | 名称 | 说明 |
| --- | --- | --- |
| `1` | `Available` | 可接受新的会话绑定 |
| `2` | `Draining` | 不再接收新会话，但允许现有会话继续运行直到结束 |
| `3` | `Unavailable` | 不可被选择为目标路由；现有绑定在检测到后应视为失效 |

**共享结构：RouteTarget**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `gameNodeId` | `string` | 目标 Game 节点的稳定逻辑身份，取值来自注册表中的 `nodeId` |
| `gameRegistrationId` | `uint64` | 目标 Game 当前有效的注册租约标识，用于识别重启后的新租约 |
| `serviceEndpoint` | `Endpoint` | Gate 连接目标 Game 使用的服务入口 |
| `routeEpoch` | `uint64` | Gate 本地路由目录版本；每次目录变更后递增 |

`RouteTarget` 表达“当前会话被绑定到哪个 Game 节点”的最小可序列化语义。`gameNodeId` 负责稳定身份，`gameRegistrationId` 负责当前活跃租约，两者不得互相替代。

**共享结构：SessionRecord**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `sessionId` | `uint64` | Gate 分配的逻辑会话标识，生命周期内唯一且非零 |
| `gateNodeId` | `string` | 当前承载该会话的 Gate 节点稳定逻辑身份 |
| `sessionState` | `uint16` | 会话生命周期状态，取值见 `SessionState` |
| `playerId` | `uint64` | 当前绑定玩家标识；未绑定时填 `0` |
| `routeState` | `uint16` | 路由状态，取值见 `RouteState` |
| `routeTarget` | `RouteTarget` | 当前绑定的目标 Game；未绑定时视为全字段无效 |
| `connectedAtUnixMs` | `uint64` | 逻辑会话创建时间 |
| `authenticatedAtUnixMs` | `uint64` | 完成鉴权或玩家装载的时间；未完成时为 `0` |
| `lastActiveUnixMs` | `uint64` | 最近一次收发客户端业务消息的时间 |
| `closedAtUnixMs` | `uint64` | 会话关闭时间；仍存活时为 `0` |
| `closeReasonCode` | `int32` | 会话关闭原因；未知或未关闭时为 `0` |

`SessionRecord` 是 Gate 内部的权威会话模型。任何会话级清理、路由判断、玩家绑定或后续实体路由扩展，都应以该记录为单一事实来源，而不是散落在连接对象、定时器或业务层中的附加字段。

其中 `routeTarget` 表达的是 Gate 会话当前命中的传输层目标 Game，而不是所有业务实体的最终 owner。对于 `PlayerEntity` 这类可迁移实体，Gate 可以在 `sessionId` / `playerId` 语义之上额外维护 `Proxy` 解析信息；对于 `SpaceEntity`、`ServerStubEntity` 这类不可迁移实体，则应交由 `Mailbox` 的静态寻址语义处理。

**共享结构：GameDirectoryEntry**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `gameNodeId` | `string` | Game 节点稳定逻辑身份，取值来自注册表中的 `nodeId` |
| `gameRegistrationId` | `uint64` | 当前有效注册租约标识，Game 每次成功重注册后都会变化 |
| `routeState` | `uint16` | Gate 视角下的可路由状态，取值见 `GameRouteState` |
| `serviceEndpoint` | `Endpoint` | Gate 与该 Game 建立内部连接使用的目标地址 |
| `capabilityTags` | `string[]` | 来自注册消息的能力标签，用于后续按能力过滤候选目标 |
| `load` | `LoadSnapshot` | 最近一次来自 GM 的负载快照 |
| `routeEpoch` | `uint64` | 该条目所属的 Gate 本地目录版本 |
| `updatedAtUnixMs` | `uint64` | Gate 最近一次应用该条目更新的时间 |

`GameDirectoryEntry` 来自 GM 维护的注册表与心跳信息，是 Gate 为新会话做路由选择时的候选目录。每个目录条目都对应同一服务器组内一个可直连的 Game 节点。它是短生命周期的运行时缓存，不应被当作稳定业务实体持久化。

**Gate 必备索引**
1. `sessionId -> SessionRecord` 是 Gate 的主索引与权威存储，任何会话查找都应先命中该表。
2. `playerId -> sessionId[]` 是按玩家回查会话的次级索引；数据模型层面允许一对多，以免把单端登录策略硬编码进基础结构。
3. `gameNodeId -> sessionId[]` 是按目标 Game 节点回查会话的反向索引，用于 Game 节点下线、租约失效或批量清理时快速定位受影响会话。
4. `gameNodeId -> GameDirectoryEntry` 是 Gate 的本地路由目录，用于候选 Game 节点查询与版本比对。
5. `gameRegistrationId -> gameNodeId` 可作为辅助索引，用于识别“同一稳定 `NodeID` 下出现了新的活跃租约”，避免把 Game 节点重启误判为原连接仍然有效。
6. 面向 `M5-08` 的实体路由实现，Gate 可以在本文件模型之上增补 `playerId -> proxy owner` 这类动态索引，用于解析可迁移实体当前所在 Game；这类索引不得替代 `SessionRecord` 与 `GameDirectoryEntry` 的基础语义。

**路由绑定与失效规则**
1. Gate 在接受客户端连接并创建逻辑会话后，应先写入 `SessionRecord`，初始状态为 `sessionState = Created`、`routeState = Unassigned`、`playerId = 0`。
2. 当会话进入鉴权、账号检查或玩家装载阶段时，应切换到 `sessionState = Authenticating`；鉴权失败可直接进入关闭流程，无需先分配路由。
3. 同一服务器组内 Gate 与 Game 采用全连接拓扑。每个 Gate 都应为路由目录中的每个 Game 节点维护直接内部连接；Game 节点之间、Gate 节点之间不直接通信。
4. 只有 `GameRouteState = Available` 且当前 Gate 已建立直接内部连接的目录条目可被用于新的会话绑定。Gate 选定目标后，应将完整的 `RouteTarget` 与当前 `routeEpoch` 写入 `SessionRecord`，随后再进入 `routeState = Bound`。
5. 当前阶段默认采用“同一会话生命周期内固定绑定到一个入口 Game 节点”的传输模型。一旦 `routeState = Bound`，同一 `sessionId` 不得静默切换到新的目标 Game 节点；重连视为新会话，由新的 `sessionId` 重新选路。可迁移实体的 owner 变化不应被误写为 `SessionRecord` 的静默传输迁移，而应通过 `Proxy` 解析层单独处理。
6. `gameNodeId` 表达稳定逻辑身份，`gameRegistrationId` 表达当前活跃租约。即便 `gameNodeId` 不变，只要 `gameRegistrationId` 变化，就应视为原先绑定的具体运行节点已经失效。
7. 当 GM 将某个 Game 节点标记为 `Draining` 时，Gate 应停止向其分配新会话，但允许既有 `Bound` 会话继续使用；当 GM 标记为 `Unavailable`，或 Gate 检测到与该 Game 节点的内部连接、租约或所有权校验失效时，应将相关会话转入 `RouteLost`。
8. `RouteLost` 只表达“原绑定目标已经不能继续承接当前会话”，不隐含自动迁移或自动重试语义。它描述的是 Gate 会话的传输层失效，不直接等价于可迁移实体完成 owner 切换。后续是否断开客户端、是否等待恢复、是否做显式补偿，应由后续里程碑条目单独定义。
9. 会话关闭时，应先把 `sessionState` 置为 `Closing`，执行反向索引清理与必要通知，再转为 `Closed`/`Released`；清理顺序必须保证不会留下悬挂的 `playerId` 或 `gameNodeId` 反向映射。

**与 Gate↔Game 封装的关系**
1. `RelayEnvelopeHeader.sessionId` 与 `RelayEnvelopeHeader.playerId` 分别复用 `SessionRecord.sessionId` 与 `SessionRecord.playerId` 语义；Game 不得自行发明新的会话标识。
2. Gate↔Game 中继封装不直接携带完整的 `RouteTarget`；目标 Game 节点由 Gate 与目标节点之间的直接内部连接隐含表达，因此 Gate 在转发前必须先验证 `SessionRecord.routeTarget` 仍然有效。
3. Game 在处理 `Relay.ForwardToGame` 时，应确认目标会话存在且当前会话所有权属于本节点。若会话不存在，应返回 `3101 Relay.SessionNotFound`；若路由或绑定关系与当前 Game 节点不匹配，应返回 `3102 Relay.RouteOwnershipMismatch`。
4. 后续如引入会话关闭、玩家绑定、路由失效等内部通知消息，其消息体字段应直接复用本文件定义的 `sessionId`、`playerId`、`gameNodeId`、`gameRegistrationId`、`routeEpoch` 与原因码语义，避免再创建一套平行命名。

**对后续条目的约束**
1. `M4-04` 的 Gate 会话管理表实现应以 `sessionId -> SessionRecord` 为权威模型，不应在连接对象上额外维护难以对齐的平行状态。
2. `M4-05` 应实现“每个 Gate 到同组全部 Game 节点”的直接连接模型，而不是经由 Gate↔Gate 或 Game↔Game 做多跳转发。
3. `M4-10` 的断线清理与 Game 通知应优先依赖 `gameNodeId -> sessionId[]` 反向索引，确保单个 Game 节点失效时可以批量定位受影响会话。
4. `M4-11` 的固定绑定策略只应定义“如何选择候选 Game 节点”，而不应改变 `RouteTarget` 与 `RouteState` 的基础语义。
5. `M4-12` 的负载感知分配可以读取 `GameDirectoryEntry.load`，但不能在没有新模型扩展的情况下把已绑定会话就地迁移到其他 Game 节点。
6. `M5-08` 的实体路由应建立在 `sessionId -> playerId` 这条链路之上，再扩展到 `PlayerEntity Proxy -> SpaceEntity Mailbox / ServerStubEntity Mailbox`；不得绕过会话模型直接把业务实体绑到裸连接句柄。实体终点的分类、单活 ownership、迁移属性与执行模型约束见 `docs/DISTRIBUTED_ENTITY.md`。
