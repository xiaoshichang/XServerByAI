# STARTUP_FLOW

本文档定义 XServerByAI 当前阶段单个服务器组的推荐启动顺序、关键阶段以及每一步涉及的跨节点消息。它只描述“谁向谁发送什么”，具体消息结构与约束仍以 `docs/PROCESS_INNER.md` 为准。

**适用范围**
1. 适用于当前单服务器组拓扑：`1 GM + N Gate + M Game`。
2. 适用于当前推荐启动顺序：`GM -> Game -> Gate -> Client`。
3. 当前阶段不定义独立配置下发消息；所有节点都从启动时传入的统一配置文件读取 `ClusterConfig` 与自身 `NodeConfig`。

**术语**
1. `Inner`：节点与节点之间的网络。
2. `Control`：ctrl 工具与 `GM` 之间的管理网络。
3. `Client`：`Gate` 与客户端之间的网络。
4. 本文涉及的启动期跨节点消息，当前都走 `Inner` 网络。

**启动总览**
1. `GM` 先启动并进入 `InnerNetwork` 监听状态。
2. `Game` 启动后先向 `GM` 完成注册与心跳闭环。
3. `GM` 在期望的 `Game` 节点注册完成后，决定 `ServerStubEntity` ownership 分配。
4. `Game` 接收 ownership 后初始化本地托管逻辑，并向 `GM` 上报 ready 结果。
5. `Gate` 启动后完成注册、心跳与 `Game` 路由目录查询。
6. `GM` 聚合 ready 状态后向 `Gate` 下发集群 ready 结论。
7. `Gate` 只有在收到 `clusterReady = true` 后，才允许打开 `ClientNetwork` 入口。

**分步流程**

| Step | Sender | Receiver | Message | Status | Description |
| --- | --- | --- | --- | --- | --- |
| `1` | `GM` | - | - | 本地启动动作 | `GM` 读取配置并初始化 `gm.innerNetwork.listenEndpoint` |
| `2` | `Game` | `GM` | `Inner.ProcessRegister (1000)` | 已登记消息 | `Game` 建立 `Inner` 链路后向 `GM` 注册自身 `NodeID`、`innerNetworkEndpoint`、版本和初始负载 |
| `3` | `GM` | `Game` | `Inner.ProcessRegister (1000)` response | 已登记消息 | `GM` 接受或拒绝注册；成功时返回心跳参数 |
| `4` | `Game` | `GM` | `Inner.ProcessHeartbeat (1100)` | 已登记消息 | `Game` 在注册成功后开始周期心跳 |
| `5` | `GM` | `Game` | `Inner.ProcessHeartbeat (1100)` response | 已登记消息 | `GM` 确认当前活动注册仍然有效 |
| `6` | `GM` | `Game` | `Inner.ServerStubOwnershipSync (1202)` | 已登记消息 | `GM` 下发当前 `ServerStubEntity -> OwnerGameNodeId` 的全量 ownership 快照 |
| `7` | `Game` | `GM` | `Inner.GameServiceReadyReport (1203)` | 已登记消息 | `Game` 上报当前 `assignmentEpoch` 下的本地 ready 结果 |
| `8` | `Gate` | `GM` | `Inner.ProcessRegister (1000)` | 已登记消息 | `Gate` 建立 `Inner` 链路后向 `GM` 注册自身 `NodeID`、`innerNetworkEndpoint` 和初始负载 |
| `9` | `GM` | `Gate` | `Inner.ProcessRegister (1000)` response | 已登记消息 | `GM` 接受或拒绝 `Gate` 注册 |
| `10` | `Gate` | `GM` | `Inner.ProcessHeartbeat (1100)` | 已登记消息 | `Gate` 在注册成功后开始周期心跳 |
| `11` | `GM` | `Gate` | `Inner.ProcessHeartbeat (1100)` response | 已登记消息 | `GM` 确认 `Gate` 当前注册仍然有效 |
| `12` | `Gate` | `GM` | `Inner.GameDirectoryQuery (1204)` | 已登记消息 | `Gate` 查询当前 `Game` 路由目录，并可请求订阅后续变化 |
| `13` | `GM` | `Gate` | `Inner.GameDirectoryQuery (1204)` response | 已登记消息 | `GM` 返回当前 `routeEpoch` 与全量 `Game` 目录 |
| `14` | `GM` | `Gate` | `Inner.ClusterReadyNotify (1201)` | 已登记消息 | `GM` 下发集群是否 ready 的结论 |
| `15` | `Gate` | - | - | 本地状态切换 | `Gate` 在 `clusterReady = true` 后打开 `gate.<NodeID>.clientNetwork.listenEndpoint` |

**最小启动消息序列**
1. `Game -> GM: Inner.ProcessRegister (1000)`
2. `GM -> Game: 1000 response`
3. `Game -> GM: Inner.ProcessHeartbeat (1100)`
4. `GM -> Game: 1100 response`
5. `GM -> Game: Inner.ServerStubOwnershipSync (1202)`
6. `Game -> GM: Inner.GameServiceReadyReport (1203)`
7. `Gate -> GM: Inner.ProcessRegister (1000)`
8. `GM -> Gate: 1000 response`
9. `Gate -> GM: Inner.ProcessHeartbeat (1100)`
10. `GM -> Gate: 1100 response`
11. `Gate -> GM: Inner.GameDirectoryQuery (1204)`
12. `GM -> Gate: 1204 response`
13. `GM -> Gate: Inner.ClusterReadyNotify (1201)`

**约束**
1. `GM` 必须在期望的 `Game` 节点注册完成后，才能最终确定 `ServerStubEntity` ownership。
2. `Game` 不得在收到 ownership 分配前自行决定本地承载哪些 `ServerStubEntity`。
3. `Gate` 不得因为本地配置齐全或局部链路可用，就自行推断客户端入口可开放。
4. `Gate` 是否开放 `Client` 网络入口，只取决于 `GM` 下发的集群级 ready 结论。
5. 当前阶段所有启动期跨节点消息都属于 `Inner` 语义，不应混用 `Control` 命名。

**关联文档**
1. 消息结构与编码规则：`docs/PROCESS_INNER.md`
2. `msgId` 登记：`docs/MSG_ID.md`
3. 项目总览：`docs/PROJECT.md`
4. 配置与日志规范：`docs/CONFIG_LOGGING.md`
