# STARTUP_FLOW

本文档定义 XServerByAI 当前阶段单个服务器组的推荐启动顺序、关键阶段以及每一步涉及的跨节点消息。它只描述“谁向谁发送什么”，具体消息结构与约束仍以 `docs/PROCESS_INNER.md` 为准。

**适用范围**
1. 适用于当前单服务器组拓扑：`1 GM + N Gate + M Game`。
2. 适用于当前推荐启动顺序：`GM -> Game -> Gate -> Client`。
3. 当前阶段所有启动期跨节点控制消息都走 `Inner` 网络；节点仍统一从启动配置文件读取 `ClusterConfig` 与自身 `NodeConfig`。

**术语**
1. `Inner`：节点与节点之间的内部网络。
2. `Control`：ctrl 工具与 `GM` 之间的管理网络。
3. `Client`：`Gate` 与客户端之间的网络。
4. `allNodesOnline`：由 `GM` 下发给 `Game` 的当前“期望节点已全部上线”结论，是 `Game -> Gate` 全连接闭环的启动信号。
5. `assignmentEpoch`：`GM` 下发 `ServerStubEntity` ownership 的轮次号。
6. `clusterReady`：由 `GM` 汇总结论后下发给所有 `Gate` 的对外服务开关，不允许由 `Gate` 本地推断。

**启动总览**
1. `GM` 先启动并进入 `InnerNetwork` 监听状态。
2. `Game` 启动后先向 `GM` 完成注册。
3. `Gate` 启动后先向 `GM` 完成注册。
4. `GM` 在期望的 `Game` 节点和 `Gate` 节点注册完成后，向所有 `Game` 通知“所有节点已经上线”。
5. 已知所有节点上线后，每个 `Game` 都要向每个 `Gate` 完成注册与心跳闭环。
6. `Game` 到 `Gate` 的全连接网络完成后，向 `GM` 报告自身 mesh ready。
7. `GM` 在聚合所有必需 `Game` 的 mesh ready 结果后，决定 `ServerStubEntity` ownership，并通知所有 `Game`。
8. `Game` 根据 `ServerStubEntity` ownership 初始化所有本地 Stub。
9. `Game` 在聚合自身所有必需 `ServerStubEntity` 的就绪结果后，向 `GM` 报告自身就绪。
10. `GM` 在聚合所有必需 `Game` 的就绪结果后，向全部 `Gate` 下发 `clusterReady = true`。
11. `Gate` 只有在收到 `clusterReady = true` 后，才允许打开 `ClientNetwork` 入口。

**分步流程**

| Step | Sender | Receiver | Message | Status | Description |
| --- | --- | --- | --- | --- | --- |
| `1` | `GM` | - | - | 本地启动动作 | `GM` 读取配置并启动 `gm.innerNetwork.listenEndpoint` 监听 |
| `2` | `Game` | `GM` | `Inner.NodeRegister (1000)` | 已登记消息 | `Game` 建立到 `GM` 的 `Inner` 主动链路后，注册自身 `NodeID`、`innerNetworkEndpoint`、版本与初始负载 |
| `3` | `GM` | `Game` | `Inner.NodeRegister (1000)` response | 已登记消息 | `GM` 接受或拒绝 `Game` 注册；成功时返回心跳参数 |
| `4` | `Game` | `GM` | `Inner.NodeHeartbeat (1100)` | 已登记消息 | `Game` 在注册成功后进入周期心跳 |
| `5` | `GM` | `Game` | `Inner.NodeHeartbeat (1100)` response | 已登记消息 | `GM` 确认 `Game` 当前注册仍然有效 |
| `6` | `Gate` | `GM` | `Inner.NodeRegister (1000)` | 已登记消息 | `Gate` 建立到 `GM` 的 `Inner` 主动链路后，注册自身 `NodeID`、`innerNetworkEndpoint`、版本与初始负载 |
| `7` | `GM` | `Gate` | `Inner.NodeRegister (1000)` response | 已登记消息 | `GM` 接受或拒绝 `Gate` 注册；成功时返回心跳参数 |
| `8` | `Gate` | `GM` | `Inner.NodeHeartbeat (1100)` | 已登记消息 | `Gate` 在注册成功后进入周期心跳 |
| `9` | `GM` | `Gate` | `Inner.NodeHeartbeat (1100)` response | 已登记消息 | `GM` 确认 `Gate` 当前注册仍然有效 |
| `10` | `GM` | `Game` | `Inner.ClusterNodesOnlineNotify (1204)` | 已登记消息 | `GM` 在期望 `Game` 与 `Gate` 节点都已完成到 `GM` 的注册后，向所有 `Game` 下发“所有节点已上线”的最新结论 |
| `11` | `Game` | `Gate` | `Inner.NodeRegister (1000)` | 已登记消息 | `Game` 针对每个目标 `Gate` 发起注册，声明自己可为该 `Gate` 提供内部转发目标 |
| `12` | `Gate` | `Game` | `Inner.NodeRegister (1000)` response | 已登记消息 | `Gate` 接受或拒绝该 `Game` 的链路注册；成功时返回心跳参数 |
| `13` | `Game` | `Gate` | `Inner.NodeHeartbeat (1100)` | 已登记消息 | `Game` 在注册成功后对该 `Gate` 进入周期心跳 |
| `14` | `Gate` | `Game` | `Inner.NodeHeartbeat (1100)` response | 已登记消息 | `Gate` 确认该 `Game` 针对自己的注册仍然有效 |
| `15` | `Game` | `GM` | `Inner.GameGateMeshReadyReport (1205)` | 已登记消息 | `Game` 在收到 `allNodesOnline = true` 并完成到全部目标 `Gate` 的注册/心跳闭环后，向 `GM` 报告自身 mesh ready |
| `16` | `GM` | `Game` | `Inner.ServerStubOwnershipSync (1202)` | 已登记消息 | `GM` 在聚合全部必需 `Game` 的 mesh ready 结果后，下发 `ServerStubEntity -> OwnerGameNodeId` 的全量 ownership 快照 |
| `17` | `Game` | - | - | 本地状态切换 | `Game` 基于最新 `assignmentEpoch` 初始化本地托管逻辑与分配给自己的 `ServerStubEntity` |
| `18` | `Game` | `GM` | `Inner.GameServiceReadyReport (1203)` | 已登记消息 | `Game` 在自身负责的 `ServerStubEntity` ready 后，向 `GM` 上报当前 `assignmentEpoch` 下的本地 ready 结果 |
| `19` | `GM` | `Gate` | `Inner.ClusterReadyNotify (1201)` | 已登记消息 | `GM` 在聚合全部必需 `Game` 的 ready 结论后，下发集群是否 ready 的结果 |
| `20` | `Gate` | - | - | 本地状态切换 | `Gate` 仅在收到最新 `clusterReady = true` 后打开 `gate.<NodeID>.clientNetwork.listenEndpoint` |

**最小启动消息序列**
1. `Game -> GM: Inner.NodeRegister (1000)`
2. `GM -> Game: 1000 response`
3. `Game -> GM: Inner.NodeHeartbeat (1100)`
4. `GM -> Game: 1100 response`
5. `Gate -> GM: Inner.NodeRegister (1000)`
6. `GM -> Gate: 1000 response`
7. `Gate -> GM: Inner.NodeHeartbeat (1100)`
8. `GM -> Gate: 1100 response`
9. `GM -> Game[*]: Inner.ClusterNodesOnlineNotify (1204)`
10. `Game -> Gate[*]: Inner.NodeRegister / Inner.NodeHeartbeat`
11. `Game -> GM: Inner.GameGateMeshReadyReport (1205)`
12. `GM -> Game[*]: Inner.ServerStubOwnershipSync (1202)`
13. `Game` 本地按 ownership 初始化 Stub
14. `Game -> GM: Inner.GameServiceReadyReport (1203)`
15. `GM -> Gate[*]: Inner.ClusterReadyNotify (1201)`

其中 `Gate[*]` 表示当前服务器组内的全部目标 `Gate` 节点。

**约束**
1. `GM` 必须在期望的 `Game` 与 `Gate` 节点都完成到 `GM` 的注册后，才能下发 `Inner.ClusterNodesOnlineNotify (1204)` 的 `allNodesOnline = true` 结论。
2. `Game` 不得在收到 `allNodesOnline = true` 前，就自行开始 `Game -> Gate` 全连接闭环。
3. `Game` 只有在完成到全部目标 `Gate` 的注册与心跳闭环后，才能向 `GM` 报告 `meshReady = true`。
4. `GM` 只有在聚合全部必需 `Game` 的 `meshReady = true` 结果后，才能最终确定 `ServerStubEntity` ownership。
5. `Game` 不得在收到 ownership 分配前自行决定本地承载哪些 `ServerStubEntity`，也不得在失去当前 `Gate` 全连接条件后继续沿用旧结论。
6. `Game` 向 `GM` 报告 `localReady = true` 前，必须先完成当前 `assignmentEpoch` 下的本地 Stub 初始化，并聚合自身所有必需 `ServerStubEntity` 的 ready 结果。
7. `Gate` 可以在启动期提前建立面向 `Game` 的 `Inner` 接入能力，但不得因为本地配置齐全、已向 `GM` 注册成功或局部链路可用，就自行推断客户端入口可开放。
8. `Gate` 是否开放 `Client` 网络入口，只取决于 `GM` 下发的最新 `clusterReady` 结论。
9. 启动期间如果 `allNodesOnline` 条件、`assignmentEpoch`、`readyEpoch` 或相关注册会话失效，相关节点必须撤销旧状态并重新走闭环，而不是混用旧结果。

**关联文档**
1. 消息结构与编码规则：`docs/PROCESS_INNER.md`
2. `msgId` 登记：`docs/MSG_ID.md`
3. 项目总览：`docs/PROJECT.md`
4. 配置与日志规范：`docs/CONFIG_LOGGING.md`
