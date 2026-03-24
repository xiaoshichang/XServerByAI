# DISTRIBUTED_ENTITY

本文档定义 XServerByAI 当前阶段采用的分布式实体架构、核心概念以及与集群启动编排相关的 ownership / ready 语义边界。后续 `M5-03` 至 `M5-16` 在实现实体基类、注册表、消息分发、路由、Tick 与持久化时，应以本文作为统一语义基线。

**适用范围**
1. 当前默认拓扑为 `1 GM + N Gate + M Game`，开发期默认单机多进程验证，也允许未来扩展到多机部署。
2. 业务实体运行在 `Game` 进程中；`GM` 负责集群编排与状态聚合，`Gate` 负责客户端接入、会话与转发。
3. 当前阶段不引入 `Game -> Game` 直连业务调用；跨节点实体访问统一经 `Gate` 中转。
4. 启动期的 `ServerStubEntity` ownership、`Game` ready 汇报与 `Gate` 客户端入口开放，都属于集群编排问题，不允许由单个 `Game` 或 `Gate` 自行推断。

**核心概念**

| 概念 | 说明 |
| --- | --- |
| `EntityKey` | 逻辑实体身份，由 `EntityType` 与领域内稳定主键组成，例如 `Player + playerId`、`Space + spaceId`。 |
| `OwnerGameNodeId` | 当前承载某个实体活动实例的 `Game` 节点标识。任意时刻一个实体只能有一个活动 owner。 |
| `Activation` | 某个逻辑实体在 `Game` 进程中的运行时实例。 |
| `MobilityType` | 实体迁移属性，取值为 `Migratable` 或 `Pinned`。 |
| `Mailbox` | 静态目标地址，适用于 `Pinned` 实体或启动期由 `GM` 分配好的 `ServerStubEntity`。它携带目标 `GameNodeId`，供 `Gate` 直接转发。 |
| `Proxy` | 动态实体引用，适用于 `Migratable` 实体。它不直接绑定最终 owner，而是依赖 `Gate` 根据最新路由解析目标。 |
| `ExecutionLane` | 实体串行执行上下文。实体消息、Tick 与异步回调都必须先回到所属 lane，再修改实体状态。 |
| `assignmentEpoch` | `GM` 下发 ownership 分配的轮次号。`Game` 只应基于当前轮次初始化本地承载。 |
| `readyEpoch` | `Game` 向 `GM` 汇报服务 ready 时使用的轮次号。旧轮次结果必须被丢弃。 |
| `clusterReady` | `GM` 聚合所有必需 `Game` 的 ready 结果后，对 `Gate` 下发的集群对外服务开关。 |

**实体分类**

| 类型 | 典型示例 | 所属进程 | 主要职责 |
| --- | --- | --- | --- |
| `ServerEntity` | `PlayerEntity`、`SpaceEntity`、`NpcEntity` | `Game` | 承载业务权威状态，维护属性、生命周期、同步与持久化边界。 |
| `ServerStubEntity` | `MatchService`、`ChatService`、`LeaderboardService` | `Game` | 承载集群内全局服务语义；其 owner 由 `GM` 在启动阶段统一分配，运行期默认不迁移。 |

`ServerStubEntity` 继承自 `ServerEntity`，但它不是“普通工具单例”，而是可被消息寻址、可参与生命周期管理、可参与 ready 判定的正式业务实体。

**启动编排与 ownership**
1. `GM` 必须先进入 `InnerNetwork` 监听状态，然后等待期望的 `Game` 与 `Gate` 节点完成到 `GM` 的注册与心跳闭环。
2. `GM` 在确认必需节点到齐后，统一决定 `ServerStubEntity -> OwnerGameNodeId` 映射，并通过 `Inner.ServerStubOwnershipSync (1202)` 下发给所有 `Game`。
3. `Game` 只能在收到最新 `assignmentEpoch` 的 ownership 后，初始化自己负责的 `ServerStubEntity` 托管逻辑；不得自行猜测或抢先承载。
4. `Game` 在本地 owned `ServerStubEntity` ready、并且已向全部目标 `Gate` 完成注册与心跳闭环后，才能通过 `Inner.GameServiceReadyReport (1203)` 向 `GM` 报告 ready。
5. `GM` 聚合所有必需 `Game` 的 ready 结果后，向全部 `Gate` 下发 `Inner.ClusterReadyNotify (1201)`。
6. `Gate` 只有在收到最新 `clusterReady = true` 后，才允许真正打开 `ClientNetwork`；它不能因为本地配置齐全、已连接 `GM`、已拿到部分目录或局部链路健康，就提前开放客户端入口。

**迁移性分类**
1. `Migratable ServerEntity` 允许显式迁移到新的 `OwnerGameNodeId`，典型示例是 `PlayerEntity`。
2. `Pinned ServerEntity` 在其生命周期内不发生 owner 切换，典型示例是 `SpaceEntity`。
3. `ServerStubEntity` 当前默认属于 `Pinned` 语义，但其 owner 并非静态写死，而是由 `GM` 在启动阶段统一分配。
4. 迁移是业务层的显式语义，不是网络层或会话层的隐式副作用。

**职责边界**
1. `GM` 负责注册表、心跳、节点存活、ownership 分配、ready 聚合与 `clusterReady` 下发，不承载业务实体活动实例。
2. `Gate` 负责客户端接入、会话管理、`Game` 目录维护与消息转发，但不持有业务权威状态，也不决定 `ServerStubEntity` ownership。
3. `Game` 负责实体活动实例、实体注册表、消息分发、Tick、持久化接入，以及本地 `ServerStubEntity` ready 判定。
4. `ServerEntity` / `ServerStubEntity` 负责业务语义与状态，不直接持有网络 socket、KCP 会话或 `Gate` 连接对象。
5. 任何共享可变业务状态都应归属于某个实体实例，禁止把权威状态散落在进程级全局单例或 `Gate` 连接对象中。

**路由与消息分发**
1. 默认业务链路为 `client session -> Gate -> PlayerEntity Proxy -> 当前 owner Game -> SpaceEntity / ServerStubEntity`。
2. 调用侧无论持有 `Mailbox` 还是 `Proxy`，都必须先检查目标实体是否在本地；本地命中时优先短路调用，否则统一经 `Gate` 转发。
3. `Mailbox` 适用于静态 owner 的实体。对 `ServerStubEntity` 来说，`Mailbox` 中的目标 `GameNodeId` 来自 `GM` 下发的 ownership，而不是本地硬编码。
4. `Proxy` 适用于可迁移实体。调用方不得把某次解析出的 owner 当作长期真值，必须允许 `Gate` 基于最新路由重新解析。
5. 当前阶段不允许 `Game -> Game` 直连业务消息。

**生命周期与执行模型**
1. 逻辑实体身份存在，不等于其运行时实例已经被装载；但一旦装载为活动实例，就必须拥有唯一 `OwnerGameNodeId`。
2. 同一实体的消息处理、Tick 推进、异步结果落地与状态修改，都必须串行化到同一 `ExecutionLane`。
3. 实体内部可以发起异步操作，但异步结果在写回状态前必须回到所属 lane。
4. `Game` 在丢失当前 `assignmentEpoch`、`readyEpoch` 或相关 `Gate` 注册闭环时，必须撤销旧轮次 ready 结果并重新参与编排。

**对后续里程碑的约束**
1. `M3-12` 必须把 ownership 视为 `GM -> Game` 的编排真值来源，而不是 `Game` 自主决策。
2. `M3-13` 必须建立 `Game -> Gate` 注册与心跳闭环，使 `Gate` 的可转发目录建立在真实会话之上。
3. `M3-14` 的 `Game` ready 必须同时满足“本地 owned Stub ready”与“已注册全部 Gate”两个条件。
4. `M3-15` 必须由 `GM` 聚合 ready 结果并下发 `clusterReady`，`Gate` 只消费结论，不重新推导结论。
5. `M4-02` 的 `ClientNetwork` 必须受 `clusterReady` 控制，禁止在 `Gate` 启动时自动开放。
6. `M4-05` 之后的转发通道必须建立在 `Game -> Gate` 注册形成的目录之上，而不是依赖旧的 `Gate -> GM` 目录查询假设。
