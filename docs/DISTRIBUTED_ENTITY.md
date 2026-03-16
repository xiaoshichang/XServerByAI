# DISTRIBUTED_ENTITY

本文档定义 XServerByAI 当前阶段 C# 业务层采用的分布式实体架构、核心概念与职责边界。后续 `M5-03` 至 `M5-16` 在实现实体基类、注册表、消息分发、实体路由、Tick、持久化与 Stub 示例时，应以本文件作为统一语义基线。

**适用范围**
1. 当前架构允许多机多进程部署，开发期默认以单机多进程形态联调与测试；业务实体默认运行在 Game 进程内，Gate 与 GM 不承载业务实体状态。
2. 当前只定义单服务器组内的实体 ownership、消息分发、迁移属性与路由关系；服务器组本身可部署在单机或多机环境中，但仍不定义跨服务器组迁移、跨机房复制或 active-active 多写模型。
3. 当前阶段不引入 Game↔Game 直接业务通信；跨节点实体调用统一复用现有内部 RPC 链路，其中动态 `Proxy` 调用需要 Gate 做二次寻址。
4. 实体架构与 Gate 会话/路由模型解耦：会话与节点目录语义见 `docs/SESSION_ROUTING.md`，实体架构只消费稳定的 `sessionId`、`playerId`、`gameNodeId` 与 `routeEpoch` 等语义。

**核心概念**

| 概念 | 说明 |
| --- | --- |
| `EntityKey` | 逻辑实体身份，由 `EntityType` 与领域内稳定主键组成；例如玩家实体可由 `Player + playerId` 表达，场景实体可由 `Space + spaceId` 表达。 |
| `MobilityType` | 实体迁移属性，取值为 `Migratable` 或 `Pinned`。只有 `Migratable` 实体允许显式切换 `OwnerGameNodeId`。 |
| `OwnerGameNodeId` | 当前持有该实体活动实例的 Game `NodeID`。它表达单活 ownership，而不是网络连接句柄或注册租约。 |
| `Activation` | 某个逻辑实体在 Game 进程内被装载后的内存实例。一个 `EntityKey` 在任意时刻最多只能存在一个活动 `Activation`。 |
| `Mailbox` | 静态实体地址。适用于 `Pinned` 实体或启动时已经固定归属的 `ServerStubEntity`；调用方无需经 Gate 做动态 owner 查询即可确定目标实体。 |
| `Proxy` | 动态实体引用。适用于 `Migratable` 实体；调用时需要先转发到 Gate，再由 Gate 按当前 ownership 解析到所在 Game。 |
| `ExecutionLane` | 实体的串行执行上下文。来自客户端消息、内部事件、Tick 与异步回调都应先进入实体所属的串行调度，再修改实体状态。 |
| `RouteHint` | 上游路由提供的实体定位线索，例如 `playerId`、`spaceId`、服务名或 shard key。它只负责帮助定位目标实体，不直接替代实体状态。 |

**实体分类**

| 类型 | 典型示例 | 所属进程 | 主要职责 |
| --- | --- | --- | --- |
| `ServerEntity` | `PlayerEntity`、`SpaceEntity`、`NpcEntity` | `Game` | 承载具备稳定业务身份的权威状态，维护属性、生命周期、状态同步与持久化边界；可再细分为 `Migratable` 与 `Pinned`。 |
| `ServerStubEntity` | `MatchService`、`ChatService`、`LeaderboardService` | `Game` | 承载全局服务语义或共享业务入口；其承载 Game 在服务器启动流程中已经确定，之后不发生迁移。 |

`ServerStubEntity` 继承自 `ServerEntity`，但语义上不是“玩家/场景这类有天然业务归属的实体”，而是“集群内可被消息寻址的服务实体”。是否采用单实例还是按 shard 部署，由具体业务决定；无论采用哪种方式，每个 Stub 实例都应有独立的 `EntityKey`，并在服务器启动或部署时固定 `OwnerGameNodeId`。

本文中的 `Space` 表示“场景”概念，用于维护场景内的玩家集合与场景状态；在房间类型游戏中，也可以将 `Space` 理解为房间概念。

**迁移性分类**
1. `Migratable ServerEntity` 允许显式迁移到新的 `OwnerGameNodeId`；典型示例是 `PlayerEntity`。迁移完成后，原 owner 必须失效，新的 owner 成为唯一活动实例。
2. `Pinned ServerEntity` 不允许在生命周期内切换承载 Game；典型示例是 `SpaceEntity`。这类实体应通过静态 `Mailbox` 寻址，而不是通过动态 `Proxy`。
3. `ServerStubEntity` 默认属于 `Pinned` 语义。其承载 Game 在服务器启动流程中已经决定，运行期不会因为负载均衡或普通路由调整而迁移。
4. 迁移是业务框架的显式操作，而不是传输层的静默副作用；是否支持某个实体类型迁移，应由框架层和实体类型共同声明。

**职责边界**
1. `Gate` 负责客户端连接、KCP 会话、鉴权、路由选择与消息转发；它可以知道 `sessionId -> playerId` 以及可迁移实体 `Proxy` 的当前 owner 线索，但不持有玩家、场景或 Stub 的权威业务状态。
2. `GM` 负责控制面、注册表、配置与节点目录；它不直接承载业务实体，不参与实体 Tick，也不作为业务消息的执行方。
3. `Game` 负责持有实体活动实例、维护实体注册表、执行消息分发、推进 Tick、驱动脏标记与衔接持久化接口，是业务状态的唯一运行时宿主。
4. `ServerEntity` 负责封装本实体的领域不变量、属性更新、状态同步钩子与保存/装载边界；框架层不应把这些规则散落到传输层或外部工具类。
5. `ServerStubEntity` 负责封装全局服务的领域语义，例如匹配、聊天或排行榜；它不是一个“随手可调的工具单例”，仍应通过实体注册表、消息分发与统一生命周期被管理。
6. 任何共享可变业务状态都应归属于某个实体实例；禁止通过进程级静态单例、裸全局字典或 Gate 连接对象持有与实体重复的权威状态。

**路由与消息分发模型**
1. 默认业务链路为 `client session -> Gate -> PlayerEntity Proxy -> current PlayerEntity -> SpaceEntity Mailbox / other ServerEntity / ServerStubEntity Mailbox`。Gate 负责把客户端请求转发到入口 Game，并为动态 `Proxy` 提供当前 owner 解析。
2. 面向玩家的客户端请求应优先落到 `PlayerEntity`；`PlayerEntity` 作为玩家上下文入口，再决定是否转发给 `SpaceEntity`、其他 `ServerEntity` 或 `ServerStubEntity`。不要让客户端直接绕过玩家上下文操纵场景或其他业务实体。
3. `Mailbox` 只用于不可迁移实体：例如 `SpaceEntity` 以及启动时已经固定归属的 `ServerStubEntity`。调用侧必须把 `Mailbox` 视为静态地址，而不是可被迁移后的软引用。
4. `Proxy` 只用于可迁移实体：例如 `PlayerEntity`。调用侧不得把某次解析得到的 `OwnerGameNodeId` 当作长期真值；跨节点调用时应重新经 Gate 解析当前 owner。
5. 若调用方与目标实体恰好位于同一个 Game，运行时可以做本地短路调用；但语义层面仍应保持 `Mailbox` 与 `Proxy` 的区分，避免把本地优化误当成地址模型。
6. 跨节点 `Mailbox` 调用可以直接按已知目标 `GameNodeId` 组织内部 RPC，不需要 Gate 做动态定位；底层是否复用统一中转链路不影响其静态寻址语义。跨节点 `Proxy` 调用则必须先经 Gate，再由 Gate 按当前 owner 转发到所在 Game。
7. `docs/MSG_ID.md` 中 `10000-19999` 对应 Player 语义、`20000-29999` 对应 Space 语义、`30000-34999` 对应 Stub / 全局服务语义；后续具体消息登记应服从这一责任域划分。

**生命周期与执行模型**
1. 实体创建分为“逻辑身份存在”和“运行时活动实例存在”两个层次。实体可以尚未装载，但一旦进入活动状态，就必须拥有唯一的 `OwnerGameNodeId`。
2. 同一个实体的消息处理、Tick 推进、异步回调落地与状态修改必须通过该实体的 `ExecutionLane` 串行化，避免多线程并发直接改写同一实体状态。
3. 实体内部可以发起异步依赖调用，但异步结果在回写实体状态前必须重新回到该实体的 `ExecutionLane`。
4. 实体不应持有网络 socket、KCP 会话对象或 Gate 连接句柄；它只消费框架提供的上下文，例如 `sessionId`、`playerId`、请求来源与路由线索。
5. `Migratable` 实体迁移时，应先冻结旧 owner 的新入站处理，再切换 `Proxy` 的 owner 解析，最后由新 owner 接管活动实例；迁移过程不得出现双活。
6. 当实体长时间空闲、显式关闭或需要回收时，框架应先完成必要的状态同步与保存，再释放该活动实例；释放后若再次访问，应通过新的装载流程恢复。

**状态同步与持久化职责**
1. `ServerEntity` 对外暴露的是权威业务状态；任何客户端可见的属性变更、场景状态变化或服务输出，都应源自实体内部状态变更，而不是旁路缓存。
2. 脏标记与同步钩子属于实体框架的一部分，用于标识“哪些状态需要推送、持久化或广播”；它们不应改变领域不变量本身。
3. 持久化接口应保持异步边界，使实体能够发起保存/加载请求而不阻塞主调度线程；具体介质由 `M5-12`、`M5-13` 另行实现。
4. `ServerStubEntity` 可以选择不持久化全部运行时状态，但若其业务需要恢复关键状态，仍应通过统一的持久化接口接入，而不是绕过框架单独落盘。

**对后续条目的约束**
1. `M5-03` 应建立以 `ServerEntity` / `ServerStubEntity` 为核心的基础项目与公共抽象，避免后续业务类型各自定义不兼容基类。
2. `M5-04` 的 `ServerEntity` 基类至少应承载稳定身份、生命周期入口、属性容器、迁移属性与框架上下文，不应直接耦合网络传输细节。
3. `M5-05` 的 `ServerStubEntity` 基类应在 `ServerEntity` 之上补充“全局服务 / 共享服务语义”，并显式声明“启动时固定承载 Game、运行期不可迁移”的约束，而不是退化成普通工具类。
4. `M5-06` 的实体注册表与查找索引应围绕 `EntityKey`、`MobilityType`、`OwnerGameNodeId`、`Mailbox` 与 `Proxy` 建立，保证单活实例查找与生命周期管理一致。
5. `M5-07` 的实体消息分发应先按 `msgId` 归属决定目标实体家族，再结合 `sessionId`、`playerId`、`spaceId`、服务键等路由线索，选择走静态 `Mailbox` 还是动态 `Proxy`。
6. `M5-08` 的实体路由必须建立在 `docs/SESSION_ROUTING.md` 的会话语义之上，默认链路为 `session -> PlayerEntity Proxy -> SpaceEntity Mailbox / ServerStubEntity Mailbox`，禁止直接把客户端连接句柄作为实体身份。
7. `M5-09` 的 `SpaceEntity` / `PlayerEntity` 应是 `ServerEntity` 的领域化实例，而不是独立于框架外的特例实现。
8. `M5-10` 的 Tick 驱动应以实体为最小调度单位，保证 Tick 回调也服从实体 `ExecutionLane` 串行执行模型。
9. `M5-11` 的脏标记与同步钩子应挂靠在实体状态变更路径上，避免在网络层或序列化层补丁式推导业务状态。
10. `M5-12` 与 `M5-13` 的持久化接口和内存适配器应服务于实体生命周期，不得把持久化模型反向耦合到传输或进程控制协议。
11. `M5-14` 与 `M5-15` 的 MatchService / ChatService 应作为 `ServerStubEntity` 示例实现，验证全局服务语义而不是额外创造第三种实体体系。
12. `M5-16` 的业务错误码映射应保持与实体责任域一致：玩家/场景错误归 `ServerEntity` 业务域，匹配/聊天等错误归 `ServerStubEntity` 业务域。
