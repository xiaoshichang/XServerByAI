# 项目说明

本文档描述 XServerByAI 当前主线代码已经实现的系统目标、节点拓扑、网络分层与主要能力，内容以当前仓库代码为准。

## 当前目标
1. 使用单一可执行程序 `xserver-node` 承载 `GM`、`Gate`、`Game` 三类节点。
2. 使用 C++ 实现配置、日志、事件循环、KCP、ZeroMQ、宿主边界与节点编排。
3. 使用 C# 实现 `Game` 侧实体框架、Stub、Avatar 业务、Mailbox / Proxy / Client RPC 等 managed 逻辑。
4. 使用单份 UTF-8 JSON 集群配置描述本地开发与多节点部署拓扑。

## 当前已落地能力

### 1. GM
- 负责 `Gate/Game -> GM` 注册与心跳。
- 汇总 `allNodesOnline`、`meshReady`、stub ownership、service ready 与 `clusterReady`。
- 暴露本地控制 HTTP：`/healthz`、`/status`、`/boardcase`、`/shutdown`。
- 能临时加载 managed `Framework` 程序集，读取 stub catalog 并参与 bootstrap ownership。

### 2. Gate
- 负责与 `GM` 的注册 / 心跳闭环。
- 维护所有 `Game` 的 inner session 与本地可路由目录。
- 暴露 `authNetwork`：`POST /login`、`GET /healthz`。
- 暴露 `clientNetwork` KCP 接入；只有在 `clusterReady = true` 后才接纳客户端。
- 当前已经支持的客户端主线消息：
  - `Client.Hello (45010)`
  - `Client.SelectAvatar (45013)`
  - `ClientToServerEntityRpc (6302)`，当前首个业务样例为 `AvatarEntity.SetWeapon`
- 当前已经支持的 `Game -> Gate` 运行期转发：
  - `Relay.PushToClient (2001)`
  - `Relay.ForwardMailboxCall (2002)`
  - `Relay.ForwardProxyCall (2005)`

### 3. Game
- 负责与 `GM` 的注册 / 心跳闭环。
- 在收到 `allNodesOnline = true` 后，与全部 `Gate` 建立 inner 注册 / 心跳闭环并汇报 mesh ready。
- 宿主管理 CLR，加载 managed runtime，并转发原生消息到 `GameNativeOnMessage(...)`。
- 当前已支持：
  - 通过 `2003 / 2004` 完成 AvatarEntity 创建握手
  - 通过 `6302` 接收客户端 entity RPC
  - 通过 `6303` 把 server entity RPC 回推给客户端

## 当前主线业务闭环
1. `GM` 启动并监听 `Inner` 与 `Control`。
2. `Gate`、`Game` 各自向 `GM` 注册并维持心跳。
3. `GM` 确认期望节点全部在线后，通知 `Game` 建立 `Game -> Gate` 全连接。
4. `Game` 完成对所有 `Gate` 的注册与心跳后，汇报 mesh ready。
5. `GM` 下发 stub ownership。
6. `Game` 初始化本地 stub，全部 ready 后向 `GM` 汇报。
7. `GM` 向全部 `Gate` 下发 `clusterReady = true`。
8. 客户端先调用 `Gate /login` 获取 KCP 会话预约，再发送 `clientHello`。
9. 客户端发送 `selectAvatar` 后，`Gate` 选择一个就绪 `Game`，发送 `2003` 创建 AvatarEntity，等待 `2004` 确认。
10. AvatarEntity 绑定完成后，客户端可以继续发送 `6302`；`Gate` 会根据当前 `avatarId -> sessionId -> gameNodeId` 路由把请求转发到目标 `Game`。
11. 当前首个完整样例为 `set-weapon gun`：client 调用 `AvatarEntity.SetWeapon`，server 更新 `Weapon`，并通过 `6303` 回推结果。

## 工程结构

### Native
- `src/native/core`
- `src/native/net`
- `src/native/ipc`
- `src/native/host`
- `src/native/node`

### Managed
- `src/managed/Foundation`
- `src/managed/Framework`
- `src/managed/GameLogic`
- `src/managed/EntityProperties.Generator`
- `src/managed/Tests/*`

### Client
- `client/XServer.Client.Framework`
- `client/XServer.Client.GameLogic`
- `client/XServer.Client.App`
- `client/Tests/XServer.Client.Tests`

## 当前边界
1. 还没有实现完整的属性同步系统。
2. 还没有实现会话迁移与跨 Gate 迁移。
3. `move` / `buyWeapon` 仍然是联调占位消息，不属于当前稳定主链路。
4. 更完整的 `Game -> Gate -> Client` 响应与错误处理会继续在后续条目中扩展。
