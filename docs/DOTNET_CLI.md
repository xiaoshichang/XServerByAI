# dotnet 常见命令行操作

本文档整理 XServerByAI 仓库当前最常用的 `dotnet` CLI 命令，重点覆盖已经存在的 managed 解决方案、服务端项目、客户端项目与测试项目。

## 当前仓库中的 .NET 项目

### 服务端 managed 解决方案

当前解决方案文件：`XServerByAI.Managed.sln`

包含项目：

1. `src/managed/Foundation/Foundation.csproj`
2. `src/managed/Framework/Framework.csproj`
3. `src/managed/GameLogic/GameLogic.csproj`
4. `src/managed/EntityProperties.Generator/EntityProperties.Generator.csproj`
5. `src/managed/Tests/Framework.Tests/Framework.Tests.csproj`
6. `src/managed/Tests/ManagedInteropAbiMismatch.Tests/ManagedInteropAbiMismatch.Tests.csproj`
7. `src/managed/Tests/ManagedInteropMissingExports.Tests/ManagedInteropMissingExports.Tests.csproj`

### 客户端项目

客户端项目当前不并入 `XServerByAI.Managed.sln`，需要单独按项目操作：

1. `client/XServer.Client.Framework/XServer.Client.Framework.csproj`
2. `client/XServer.Client.GameLogic/XServer.Client.GameLogic.csproj`
3. `client/XServer.Client.App/XServer.Client.App.csproj`
4. `client/Tests/XServer.Client.Tests/XServer.Client.Tests.csproj`

### 当前目标框架

1. `Foundation`、`Framework`、`GameLogic`、managed tests 与 client projects 当前目标框架都是 `net10.0`。
2. `EntityProperties.Generator` 当前目标框架是 `netstandard2.0`。

## 环境检查

```powershell
dotnet --version
dotnet --info
dotnet --list-sdks
dotnet --list-runtimes
```

## 常用服务端命令

### 还原

```powershell
dotnet restore .\XServerByAI.Managed.sln
```

### 构建

```powershell
dotnet build .\XServerByAI.Managed.sln -c Debug
dotnet build .\XServerByAI.Managed.sln -c Release
```

### 测试

```powershell
dotnet test .\XServerByAI.Managed.sln -c Debug
dotnet test .\XServerByAI.Managed.sln -c Release
```

### 列出解决方案项目

```powershell
dotnet sln .\XServerByAI.Managed.sln list
```

## 常用客户端命令

### 构建客户端控制台程序

```powershell
dotnet build .\client\XServer.Client.App\XServer.Client.App.csproj -c Debug
```

### 运行客户端控制台程序

```powershell
dotnet run --project .\client\XServer.Client.App\XServer.Client.App.csproj -c Debug
```

### 运行客户端测试

```powershell
dotnet test .\client\Tests\XServer.Client.Tests\XServer.Client.Tests.csproj -c Debug
```

## 单项目定位命令

### 只构建 `Framework`

```powershell
dotnet build .\src\managed\Framework\Framework.csproj -c Debug
```

### 只构建 `GameLogic`

```powershell
dotnet build .\src\managed\GameLogic\GameLogic.csproj -c Debug
```

### 只跑 `Framework.Tests`

```powershell
dotnet test .\src\managed\Tests\Framework.Tests\Framework.Tests.csproj -c Debug
```

### 只跑 ABI / 缺导出负面测试

```powershell
dotnet test .\src\managed\Tests\ManagedInteropAbiMismatch.Tests\ManagedInteropAbiMismatch.Tests.csproj -c Debug
dotnet test .\src\managed\Tests\ManagedInteropMissingExports.Tests\ManagedInteropMissingExports.Tests.csproj -c Debug
```

## 解决方案与引用管理

### 向解决方案添加项目

```powershell
dotnet sln .\XServerByAI.Managed.sln add .\src\managed\Framework\Framework.csproj
```

### 从解决方案移除项目

```powershell
dotnet sln .\XServerByAI.Managed.sln remove .\src\managed\Framework\Framework.csproj
```

### 添加项目引用

```powershell
dotnet add .\src\managed\GameLogic\GameLogic.csproj reference .\src\managed\Framework\Framework.csproj
```

### 移除项目引用

```powershell
dotnet remove .\src\managed\GameLogic\GameLogic.csproj reference .\src\managed\Framework\Framework.csproj
```

## NuGet 包操作

### 添加包

```powershell
dotnet add .\src\managed\Framework\Framework.csproj package Newtonsoft.Json
```

### 指定版本添加包

```powershell
dotnet add .\src\managed\Framework\Framework.csproj package Newtonsoft.Json --version 13.0.3
```

### 移除包

```powershell
dotnet remove .\src\managed\Framework\Framework.csproj package Newtonsoft.Json
```

### 查看项目包引用

```powershell
dotnet list .\src\managed\Framework\Framework.csproj package
```

## 格式化与清理

```powershell
dotnet clean .\XServerByAI.Managed.sln -c Debug
dotnet format .\XServerByAI.Managed.sln
```

## 当前仓库的推荐日常流程

### 服务端 managed 回归

```powershell
dotnet restore .\XServerByAI.Managed.sln
dotnet build .\XServerByAI.Managed.sln -c Debug
dotnet test .\XServerByAI.Managed.sln -c Debug
```

### 客户端回归

```powershell
dotnet build .\client\XServer.Client.App\XServer.Client.App.csproj -c Debug
dotnet test .\client\Tests\XServer.Client.Tests\XServer.Client.Tests.csproj -c Debug
```

## 与 `configs/local-dev.json` 的关系

1. 当前 [configs/local-dev.json](/C:/Users/xiao/Documents/GitHub/XServerByAI/configs/local-dev.json) 默认指向：
   - `src/managed/Framework/bin/Debug/net10.0/XServer.Managed.Framework.dll`
   - `src/managed/Framework/bin/Debug/net10.0/XServer.Managed.Framework.runtimeconfig.json`
   - `src/managed/GameLogic/bin/Debug/net10.0/XServer.Managed.GameLogic.dll`
2. 因此本地联调前，至少需要先构建 `Framework` 与 `GameLogic` 的 `Debug` 产物。
3. 如果改用 `Release` 构建，需要同步修改配置文件中的 managed 路径。

## 常见问题

### 1. 为什么 `dotnet run` 不能直接跑 `Framework` 或 `GameLogic`？

因为这两个项目当前都是类库，不是可执行入口。当前可直接运行的是客户端控制台项目 `client/XServer.Client.App`。

### 2. 为什么客户端项目没有出现在 `XServerByAI.Managed.sln` 里？

这是当前仓库结构的设计选择。服务端 managed 代码与客户端代码分开维护，因此客户端项目需要按 `.csproj` 单独构建和测试。

### 3. 为什么本地集群启动时提示找不到 managed DLL？

通常是因为还没有先执行 `dotnet build` 生成 `Debug/net10.0` 产物，或者配置文件里的路径与实际构建配置不一致。
