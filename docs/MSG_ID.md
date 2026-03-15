# MSG_ID

本文档定义 XServerByAI 当前阶段 `msgId` 的分段方式、规范命名方式与登记规则。所有内部协议、Gate↔Client 协议与后续 C# 实体消息在分配编号前都应先更新本文件。

**基础规则**
1. `msgId` 使用 `uint32_t` 表示，`0` 为保留值，表示“无效 / 未初始化消息”。
2. `msgId` 只表达“消息语义”；响应沿用请求原始 `msgId`，并通过 `PacketHeader.flags.Response` 表示响应语义。
3. 错误语义通过 `PacketHeader.flags.Error` 表示，不单独创建成对的 `ErrorResponse` `msgId`；具体错误码范围与编码规则见 `docs/ERROR_CODE.md`。
4. 同一个消息一旦对外分配编号，不得复用；废弃时仅标记为 `Deprecated` 并保留历史记录。
5. 生产协议只能使用稳定号段；临时联调或测试消息必须进入测试号段，联调结束后要么删除，要么重新走正式分配。

**顶层号段**

| 范围 | 类别 | 用途 |
| --- | --- | --- |
| `1-999` | 协议保留 | 协议层公共语义、基础兼容控制、通用保留位配套消息 |
| `1000-1999` | 控制面 | GM ↔ Gate/Game 的注册、心跳、配置、路由与集群控制消息 |
| `2000-3999` | 内部中转 | Gate ↔ Game 封装、会话事件、内部 RPC 与转发消息 |
| `4000-9999` | 客户端接入 | Gate ↔ Client 的鉴权、会话、基础服务与通用推送 |
| `10000-19999` | Player 业务 | 玩家实体与玩家域业务消息 |
| `20000-29999` | Space 业务 | 场景实体与场景域业务消息 |
| `30000-34999` | Stub / 全局服务 | `ServerStubEntity` 全局服务，例如匹配、聊天、排行榜 |
| `35000-39999` | 共享业务 | 跨实体共享的业务公共消息与通用事件 |
| `40000-44999` | 运维 / 诊断 | 后台管理、可观测性、诊断与维护消息 |
| `45000-49999` | 测试 / 联调 | 临时验证、回归探针、实验性协议 |
| `50000+` | 预留 | 未来多机扩展、大版本迁移或新增协议域 |

**当前预留子段**

| 范围 | 预留对象 | 说明 | 关联条目 |
| --- | --- | --- | --- |
| `1000-1099` | 进程注册 / 注销 | GM 控制面的进程生命周期消息 | `M1-09`, `M3-04` |
| `1100-1199` | 心跳 / 健康检查 | 心跳、超时探测、状态上报 | `M1-09`, `M3-05` |
| `1200-1299` | 配置 / 路由下发 | GM 下发配置、Game 节点路由目录、节点列表快照与路由变更 | `M3-06`, `M3-12`, `M3-15` |
| `2000-2099` | Gate↔Game 封装 | 请求转发、响应回传、内部 RPC 信封 | `M1-10`, `M4-06`, `M4-07` |
| `2100-2199` | 会话事件 | 绑定、关闭、路由丢失、踢出、重连等会话与路由状态变化 | `M1-12`, `M4-10`, `M4-16`, `M5-08` |
| `4000-4199` | 客户端会话基础 | 鉴权、客户端心跳、基础 Push | `M4-03`, `M4-15` |
| `10000-10999` | Player 实体核心 | 玩家实体命令、查询与状态同步 | `M5-07`, `M5-09` |
| `20000-20999` | Space 实体核心 | 场景创建、进入、离开、状态同步 | `M5-07`, `M5-09` |
| `30000-30499` | MatchService | 匹配服务 `ServerStubEntity` 消息 | `M5-14` |
| `30500-30999` | ChatService | 聊天服务 `ServerStubEntity` 消息 | `M5-15` |

以上表格只表示“子段预留”，不表示具体消息已经完成分配。后续条目在定义具体消息结构时，应在所属子段内补充精确 `msgId`。

其中会话与路由相关消息在落具体结构时，应复用 `docs/SESSION_ROUTING.md` 中的 `sessionId`、`playerId`、`gameNodeId`、`gameRegistrationId` 与 `routeEpoch` 语义，避免为同一条路由关系创造多套字段命名。

其中 `10000-34999` 业务号段的责任域划分应与 `docs/DISTRIBUTED_ENTITY.md` 保持一致：`Player` / `Space` 等状态型业务消息落在 `ServerEntity` 语义下，`Stub / 全局服务` 号段保留给 `ServerStubEntity` 语义，避免把传输层中继消息与实体业务消息混放。

`Mailbox` 与 `Proxy` 只影响实体的寻址与转发路径，不改变业务 `msgId` 的责任域归属：面向 `PlayerEntity Proxy` 的调用仍登记在 Player 号段，面向 `SpaceEntity Mailbox` 的调用仍登记在 Space 号段。若后续需要为 `Proxy` 定位、Gate 二次寻址或转发附加元数据分配消息，应落入 `2000-3999` 内部中转号段，而不是占用业务号段。

**已登记控制面消息**

| msgId | CanonicalName | Direction | Owner | Status | Description |
| --- | --- | --- | --- | --- | --- |
| `1000` | `Control.ProcessRegister` | `Gate/Game -> GM` | `gm` | `Active` | 进程注册请求；成功或失败响应都复用同一 `msgId` |
| `1100` | `Control.ProcessHeartbeat` | `Gate/Game -> GM` | `gm` | `Active` | 注册后的周期心跳与轻量负载上报；响应复用同一 `msgId` |

**已登记 Relay 消息**

| msgId | CanonicalName | Direction | Owner | Status | Description |
| --- | --- | --- | --- | --- | --- |
| `2000` | `Relay.ForwardToGame` | `Gate -> Game` | `gate` | `Active` | Gate 将客户端请求中继到已绑定 Game；响应复用同一 `msgId` |
| `2001` | `Relay.PushToClient` | `Game -> Gate` | `game` | `Active` | Game 发起的单向下行推送，由 Gate 重新封装后发送给客户端 |

**命名规范**
1. 每个消息都应维护一个规范英文名，使用 `PascalCase` 片段并以 `.` 分隔，格式为 `<Area>.<Action>` 或 `<Area>.<Subject>.<Action>`。
2. `<Area>` 必须与所属号段的责任域一致，例如 `Control.ProcessRegister`、`Relay.ForwardToGame`、`Player.LoadProfile`、`Space.SyncState`、`Match.Enqueue`。
3. 请求 / 查询 / 命令名称不追加 `Request` 或 `Response` 后缀；响应使用相同 `msgId`，只通过 `Response` 标志位区分。
4. 单向事件统一使用 `Notify` 后缀；面向客户端的主动下行消息统一使用 `Push` 后缀；仅在明确扇出语义时使用 `Broadcast`。
5. 避免使用 `Handle`、`Do`、`Process` 这类宽泛动词，优先使用 `Register`、`Heartbeat`、`Forward`、`Join`、`Leave`、`Sync` 等领域动词。
6. 缩写只允许使用已在项目中固定的名词，例如 `GM`、`Gate`、`Game`、`KCP`；其余名称优先使用完整单词。
7. 文档中的规范英文名应直接映射为代码常量名，去掉 `.` 后保持 `PascalCase`，例如 `Control.ProcessRegister` → `ControlProcessRegister`。

**登记要求**
1. 任何新增正式 `msgId` 的功能条目，都必须在本文件追加登记记录后才能合入。
2. 单条登记至少包含：数值编号、规范英文名、方向、Owner、状态、简要说明。
3. 如果消息体结构发生不兼容变化，应分配新的 `msgId` 并保留旧编号，不能静默改写旧语义。
4. 日志、metrics 与抓包说明应尽量同时输出数值编号与规范英文名，降低跨语言排障成本。

**登记模板**

| msgId | CanonicalName | Direction | Owner | Status | Description |
| --- | --- | --- | --- | --- | --- |
| `<value>` | `<Area>.<Action>` | `<caller -> callee>` | `<owner>` | `<status>` | `<summary>` |
