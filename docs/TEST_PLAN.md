# 测试计划

本文档整理 XServerByAI 当前主线代码的测试类型、推荐执行顺序与联调步骤，内容以当前仓库已经存在的测试资产和命令为准。

## 推荐执行顺序

### 1. Native 回归
```powershell
cmake -S . -B build -DXS_BUILD_TESTS=ON
cmake --build .\build --config Debug --target xserver_node -- /m:1
ctest --test-dir .\build -C Debug --output-on-failure
```

说明：
- 本轮 `M4-06` 涉及 `GateNode.cpp`，native 构建必须至少通过一次。
- 当前环境下如果 MSBuild 因 `Path/PATH` 重复导致 `CL.exe` 初始化失败，需要先清理进程内的 `PATH` 重复项再执行构建。

### 2. Managed Framework 回归
```powershell
dotnet test .\src\managed\Tests\Framework.Tests\Framework.Tests.csproj -c Debug --tl:off -m:1
```

说明：
- managed 测试在当前机器上建议固定使用 `-m:1`，避免并行构建导致的偶发问题。
- 若出现 `NU1900`，通常是因为当前环境无法访问 `nuget.org` 的漏洞元数据服务，不影响本地功能验证。

### 3. Client 回归
```powershell
dotnet test .\client\Tests\XServer.Client.Tests\XServer.Client.Tests.csproj -c Debug --tl:off
```

## 当前自动化覆盖重点

### Native
- JSON / 配置解析
- 定时器与事件循环
- Packet / Relay / Register / Heartbeat 编解码
- KCP peer
- ZeroMQ active / passive connector
- managed host runtime

### Managed
- `GameNativeExports`
- `GameNodeRuntimeState`
- `EntityManager`
- `ProxyCallRouting`
- `StubCallRouting`
- `EntityRpcFramework`

### Client
- `GateAuthClient`
- `ClusterClientConfigLoader`
- `MinimalKcpSession`
- `PacketCodec`
- `ClientRuntimeState`
- `ClientGameLogicService`
- `ClientEntityRpc`

## M4-06 定向验证

### 自动化
1. `ClientEntityRpcPacketSender` 能把 entity RPC 包装成真实客户端包头。
2. client 能处理 `OnSetWeaponResult(string weapon, bool succ)` 并更新本地 `Weapon`。
3. managed `AvatarEntity.SetWeapon` 会更新服务端 `Weapon`，并回推正确的 client RPC 参数。

### 联调步骤
```powershell
dotnet run --project .\client\XServer.Client.App\XServer.Client.App.csproj
```

建议最小流程：
1. `login http://127.0.0.1:4101 demo-account dev-password`
2. `connect`
3. `selectAvatar`
4. `set-weapon gun`
5. `status`

### 预期结果
1. client 输出 `set-weapon rpc sent msgId=6302 ...`
2. `Gate` 记录 “forwarded client entity RPC to Game”
3. `Game` 记录 `AvatarEntity ... updated Weapon ...`
4. client 收到 `OnSetWeaponResult` 后，`status` 中显示 `weapon=gun`

## 当前测试空白
1. 还没有针对 `Gate` 客户端 RPC 转发链路的独立 C++ 单元测试。
2. 还没有形成覆盖 `login -> connect -> selectAvatar -> set-weapon` 的自动化端到端测试。
3. `move` 仍主要用于联调占位，不属于当前稳定主链路。
