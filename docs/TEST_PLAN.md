# 测试计划

本文档整理 XServerByAI 当前主线代码已经落地的测试类型、推荐执行顺序与仍然存在的测试空白。本文档以当前仓库真实测试资产为准，不再停留在“仅按里程碑规划”的状态。

## 目标

1. 保证 native 配置、日志、ZeroMQ、KCP、协议编解码与节点宿主逻辑可回归。
2. 保证 managed ABI、stub catalog、ownership 同步、avatar 创建与 proxy/mailbox 路由可回归。
3. 保证客户端登录、KCP 会话、脚本化启动流程与本地联调入口可用。

## 当前测试资产

### 1. C++ 测试

目录：`tests/cpp/`

当前通过 CMake + CTest 管理，覆盖内容包括：

1. JSON / 配置解析
2. 时间与事件循环
3. 序列化、包头与消息分发
4. KCP peer
5. register / heartbeat / inner cluster codec
6. relay codec
7. ZeroMQ active/passive connector
8. CLR 宿主与 managed interop runtime

### 2. Python 测试

目录：`tests/python/test_cluster_ctl.py`

当前覆盖：

1. `tools/cluster_ctl.py` 的配置解析
2. `start` 启动顺序与 GM 探活
3. `status` 轮询阶段判断
4. `kill` 进程匹配与清理

### 3. managed xUnit 测试

目录：`src/managed/Tests/`

当前包含：

1. `Framework.Tests`
   - `GameNativeExports`
   - `GameNodeRuntimeState`
   - `EntityManager`
   - `ProxyCallRouting`
   - `StubCallRouting`
   - `ServerStubCatalog`
2. `ManagedInteropAbiMismatch.Tests`
3. `ManagedInteropMissingExports.Tests`

### 4. 客户端 xUnit 测试

目录：`client/Tests/XServer.Client.Tests`

当前覆盖：

1. `GateAuthClient`
2. `ClusterClientConfigLoader`
3. `MinimalKcpSession`
4. `PacketCodec`
5. `ClientRuntimeState`
6. `ClientGameLogicService`

## 推荐执行顺序

### 基础回归

```powershell
cmake -S . -B build -DXS_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

适用于：

1. native 配置、协议、ZeroMQ、KCP 与宿主层回归
2. Python `cluster_ctl` 测试
3. 通过 CMake 挂接的 managed interop 相关 native 测试

### managed 回归

```powershell
dotnet restore .\XServerByAI.Managed.sln
dotnet test .\XServerByAI.Managed.sln -c Debug
```

适用于：

1. `Foundation`
2. `Framework`
3. `GameLogic`
4. `EntityProperties.Generator`
5. `src/managed/Tests/*`

### 客户端回归

```powershell
dotnet test .\client\Tests\XServer.Client.Tests\XServer.Client.Tests.csproj -c Debug
```

适用于客户端登录、配置读取、KCP 会话与本地游戏逻辑辅助层。

## 联调与冒烟建议

### 本地集群启动

```powershell
python .\tools\cluster_ctl.py start --config .\configs\local-dev.json
```

或直接使用包装脚本：

```powershell
.\tools\start_local_cluster.bat
```

### 本地客户端冒烟

```powershell
dotnet run --project .\client\XServer.Client.App\XServer.Client.App.csproj
```

建议最小流程：

1. `login http://127.0.0.1:4101 demo-account dev-password`
2. `connect`
3. `selectAvatar`
4. 观察 `selectAvatarResult` 是否成功回到客户端

## 当前重点验证点

1. `GM` 启动编排：
   - `allNodesOnline`
   - `meshReady`
   - `ownership`
   - `clusterReady`
2. `Gate` 接入：
   - `/login` 授权
   - KCP 会话建立
   - `clientHello`
   - `selectAvatar`
3. `Game` 宿主：
   - ABI 版本校验
   - `GameNativeInit`
   - stub catalog
   - ownership 应用
   - avatar 创建
4. 运行期中继：
   - `Relay.ForwardMailboxCall`
   - `Relay.ForwardProxyCall`
   - `Relay.PushToClient`

## 当前测试空白

1. 还没有覆盖完整的长期稳定性长跑测试。
2. 还没有形成多机部署下的自动化回归。
3. 客户端 `move` / `buyWeapon` 仍主要是联调占位消息，尚未形成稳定的端到端自动化断言。
4. 性能基线与容量测试还未形成固定脚本。

## 通过标准

1. `ctest --output-on-failure` 全绿。
2. `dotnet test .\XServerByAI.Managed.sln` 全绿。
3. `dotnet test .\client\Tests\XServer.Client.Tests\XServer.Client.Tests.csproj` 全绿。
4. 本地集群最小冒烟能够完成 `GM -> Game -> Gate -> client` 的启动闭环与 avatar 选择确认。
