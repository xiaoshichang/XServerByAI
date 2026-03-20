# STARTUP_FLOW

本文档定义 XServerByAI 当前设计下单个服务器组的推荐启动顺序、关键阶段以及每一步涉及的跨节点消息。它关注“谁向谁发送什么消息”，不展开消息体编码细节；具体字段、约束与时序规则仍以 `docs/PROCESS_CONTROL.md` 为准。

**适用范围**
1. 适用于当前单个服务器组拓扑：`1 GM + N Gate + M Game`。
2. 适用于当前推荐启动顺序：`GM -> Game -> Gate -> Client`。
3. 当前阶段不定义独立的配置下发消息；所有节点都从进程启动时传入的统一配置文件读取整个集群配置以及自身实例配置。
4. 除“本地启动动作 / 本地调度动作 / 本地状态切换”这类纯进程内步骤外，本文涉及的跨节点消息均已在 `docs/MSG_ID.md` 与 `docs/PROCESS_CONTROL.md` 中完成稳定登记。

**术语约定**
1. “本地启动动作”表示只发生在节点进程内部，不产生跨节点消息。
2. “已登记消息”表示该消息已经在 `docs/MSG_ID.md` / `docs/PROCESS_CONTROL.md` 中拥有稳定编号与结构定义。

**启动总览**
1. `GM` 先启动并进入控制面监听状态。
2. `Game` 再启动，先完成向 `GM` 的注册与心跳闭环。
3. 当所有期望的 `Game` 节点都注册完成后，`GM` 统一决定每一个 `ServerStubEntity` 应该分配给哪个 `GameNodeId`。
4. `GM` 把 `ServerStubEntity` ownership 分配结果下发给各个 `Game`；每个 `Game` 仅初始化被分配给自己的 Stub。
5. 每个 `Game` 在本地 assigned `ServerStubEntity` 全部 ready 后，向 `GM` 上报本地 ready 结果。
6. `Gate` 启动后向 `GM` 完成注册与心跳，并查询当前 `Game` 路由目录；如需要持续接收目录变化，则同时建立订阅。
7. `GM` 聚合所有 `Game` 的 ready 结果后，向 `Gate` 下发集群 ready 结论。
8. 只有在收到 `clusterReady = true` 后，`Gate` 才允许打开客户端连接入口。

**分步流程**

| Step | Sender | Receiver | Message | Status | Description |
| --- | --- | --- | --- | --- | --- |
| `1` | `GM` | - | - | 本地启动动作 | `GM` 读取启动配置文件，初始化控制面监听端口并进入运行状态 |
| `2` | `Game` | `GM` | `Control.ProcessRegister (1000)` | 已登记消息 | `Game` 建立控制链路后先向 `GM` 注册自身 `NodeID`、`serviceEndpoint`、构建版本和初始负载 |
| `3` | `GM` | `Game` | `Control.ProcessRegister (1000)` response | 已登记消息 | `GM` 接受或拒绝注册；成功时返回心跳间隔、超时阈值和服务器时间 |
| `4` | `Game` | `GM` | `Control.ProcessHeartbeat (1100)` | 已登记消息 | `Game` 在注册成功后开始周期心跳并上报轻量负载 |
| `5` | `GM` | `Game` | `Control.ProcessHeartbeat (1100)` response | 已登记消息 | `GM` 确认当前活动注册仍然有效，并可回传新的心跳参数 |
| `6` | `GM` | - | - | 本地调度动作 | `GM` 根据启动配置中声明的期望 `Game` 节点集合，等待全部 `Game` 注册完成后，决定每一个 `ServerStubEntity` 的 `OwnerGameNodeId` |
| `7` | `GM` | `Game` | `Control.ServerStubOwnershipSync (1202)` | 已登记消息 | `GM` 下发当前 `ServerStubEntity -> OwnerGameNodeId` 的全量 ownership 快照；每个 `Game` 只初始化其中 `ownerGameNodeId == localNodeId` 的 Stub |
| `8` | `Game` | - | - | 本地启动动作 | `Game` 初始化托管层，并按 `GM` 下发的 ownership 结果创建本地 assigned `ServerStubEntity`，在本地判定“本进程 assigned Stub 是否全部 ready” |
| `9` | `Game` | `GM` | `Control.GameServiceReadyReport (1203)` | 已登记消息 | `Game` 把本地 assigned `ServerStubEntity ready` 聚合结果上报给 `GM`，并带上当前 `assignmentEpoch` |
| `10` | `Gate` | `GM` | `Control.ProcessRegister (1000)` | 已登记消息 | `Gate` 建立控制链路后向 `GM` 注册自身 `NodeID`、服务入口和初始负载 |
| `11` | `GM` | `Gate` | `Control.ProcessRegister (1000)` response | 已登记消息 | `GM` 接受或拒绝 `Gate` 注册；成功时返回心跳参数 |
| `12` | `Gate` | `GM` | `Control.ProcessHeartbeat (1100)` | 已登记消息 | `Gate` 在注册成功后开始周期心跳 |
| `13` | `GM` | `Gate` | `Control.ProcessHeartbeat (1100)` response | 已登记消息 | `GM` 确认 `Gate` 当前注册与控制链路仍然有效 |
| `14` | `Gate` | `GM` | `Control.GameDirectoryQuery (1204)` | 已登记消息 | `Gate` 请求当前 `Game` 路由目录全量快照；如需后续持续接收变化，则在请求中设置 `subscribeUpdates = true` |
| `15` | `GM` | `Gate` | `Control.GameDirectoryQuery (1204)` response | 已登记消息 | `GM` 返回当前 `routeEpoch` 与全量 `Game` 路由目录；若接受订阅，后续目录变化改用 `Control.GameDirectoryNotify (1205)` |
| `16` | `GM` | `Gate` | `Control.ClusterReadyNotify (1201)` | 已登记消息 | `GM` 聚合所有 `Game` 的 ready 结果后，把“集群是否允许对外提供服务”的结论下发给 `Gate` |
| `17` | `Gate` | - | - | 本地状态切换 | `Gate` 收到 `clusterReady = true` 后开放客户端入口；收到 `false` 或尚未收到通知时保持关闭 |

**最小启动消息序列**
1. `Game -> GM: Control.ProcessRegister (1000)`
2. `GM -> Game: 1000 response`
3. `Game -> GM: Control.ProcessHeartbeat (1100)`
4. `GM -> Game: 1100 response`
5. `GM -> Game: Control.ServerStubOwnershipSync (1202)`
6. `Game -> GM: Control.GameServiceReadyReport (1203)`
7. `Gate -> GM: Control.ProcessRegister (1000)`
8. `GM -> Gate: 1000 response`
9. `Gate -> GM: Control.ProcessHeartbeat (1100)`
10. `GM -> Gate: 1100 response`
11. `Gate -> GM: Control.GameDirectoryQuery (1204)`  
说明：启动阶段推荐设置 `subscribeUpdates = true`
12. `GM -> Gate: 1204 response`
13. `GM -> Gate: Control.ClusterReadyNotify (1201)`

**已正式定义的启动相关消息**

| msgId | CanonicalName | Direction | Purpose |
| --- | --- | --- | --- |
| `1000` | `Control.ProcessRegister` | `Gate/Game -> GM` | 节点注册 |
| `1100` | `Control.ProcessHeartbeat` | `Gate/Game -> GM` | 注册后的周期心跳 |
| `1201` | `Control.ClusterReadyNotify` | `GM -> Gate/Game` | GM 下发集群 ready 结论 |
| `1202` | `Control.ServerStubOwnershipSync` | `GM -> Game` | GM 下发 `ServerStubEntity` ownership 全量快照 |
| `1203` | `Control.GameServiceReadyReport` | `Game -> GM` | `Game` 上报本地 assigned `ServerStubEntity` ready 聚合结果 |
| `1204` | `Control.GameDirectoryQuery` | `Gate -> GM` | `Gate` 查询当前 `Game` 路由目录，并可请求建立后续订阅 |
| `1205` | `Control.GameDirectoryNotify` | `GM -> Gate` | `GM` 在 `routeEpoch` 变化时向已订阅 `Gate` 推送新的全量目录快照 |

**约束**
1. `GM` 必须在所有期望的 `Game` 节点注册完成后，才能最终确定每一个 `ServerStubEntity` 的 `OwnerGameNodeId`。
2. `Game` 不得在收到 `GM` 的 ownership 分配结果前，自行决定本地应该承载哪些 `ServerStubEntity`。
3. `Game` 上报的 `Control.GameServiceReadyReport.assignmentEpoch` 必须与最近一次收到并接受的 `Control.ServerStubOwnershipSync.assignmentEpoch` 一致；旧 epoch 的 ready 上报不得覆盖新结论。
4. `Gate` 不得因为本地配置、单个节点注册成功或局部链路可用，就自行推断客户端入口可以开放。
5. `Gate` 的客户端入口是否可开放，只取决于 `GM` 下发的集群级 ready 结论。
6. 即使 `Gate` 启动早于某些 `Game`，也必须保持客户端入口关闭，直到收到 `Control.ClusterReadyNotify(clusterReady = true)`。
7. 当前阶段所有配置均来自启动配置文件，而不是运行时控制面配置推送。

**关联文档**
1. 消息结构与编码规则：`docs/PROCESS_CONTROL.md`
2. `msgId` 登记：`docs/MSG_ID.md`
3. 项目总览：`docs/PROJECT.md`
4. 分布式实体与 `ServerStubEntity ready` 语义：`docs/DISTRIBUTED_ENTITY.md`
