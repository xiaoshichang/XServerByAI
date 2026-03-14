# dotnet 常见命令行操作

本文档整理 `dotnet` CLI 的常见操作，适合作为日常开发速查表。  
示例默认使用 PowerShell，路径以当前仓库的 `src/managed/` 目录为参考。

**适用范围**
1. 创建 .NET 解决方案与项目。
2. 还原依赖、构建、运行、测试与发布。
3. 管理 NuGet 包、项目引用与常见辅助命令。

**前置说明**
1. `dotnet` 命令来自 .NET SDK，不是单独的运行时。
2. 先确认本机已安装 SDK，再执行创建、构建、测试等命令。
3. 如果目录里同时存在多个 `.sln` 或 `.csproj`，建议显式指定目标文件，避免命令作用到错误对象。

## 环境检查

**查看 SDK 版本**
```powershell
dotnet --version
```

**查看 SDK 与运行时详情**
```powershell
dotnet --info
```

**列出已安装 SDK**
```powershell
dotnet --list-sdks
```

**列出已安装运行时**
```powershell
dotnet --list-runtimes
```

## 创建项目

**查看可用模板**
```powershell
dotnet new list
```

**创建解决方案**
```powershell
dotnet new sln -n XServerByAI.Managed -f sln
```

**创建类库项目**
```powershell
dotnet new classlib -n Common -o .\src\managed\Common
```

**创建控制台项目**
```powershell
dotnet new console -n ServerTool -o .\src\managed\ServerTool
```

**创建 xUnit 测试项目**
```powershell
dotnet new xunit -n Common.Tests -o .\src\managed\Tests\Common.Tests
```

**查看某个模板的参数**
```powershell
dotnet new console -h
```

**常见模板**
1. `console`：控制台应用。
2. `classlib`：类库。
3. `xunit` / `nunit` / `mstest`：测试项目。
4. `webapi`：ASP.NET Core Web API。
5. `worker`：后台服务。

## 管理解决方案与项目引用

**将项目加入解决方案**
```powershell
dotnet sln .\XServerByAI.Managed.sln add .\src\managed\Common\Common.csproj
dotnet sln .\XServerByAI.Managed.sln add .\src\managed\GameLogic\GameLogic.csproj
```

**查看解决方案中的项目**
```powershell
dotnet sln .\XServerByAI.Managed.sln list
```

**从解决方案中移除项目**
```powershell
dotnet sln .\XServerByAI.Managed.sln remove .\src\managed\GameLogic\GameLogic.csproj
```

**给项目添加项目引用**
```powershell
dotnet add .\src\managed\GameLogic\GameLogic.csproj reference .\src\managed\Common\Common.csproj
```

**移除项目引用**
```powershell
dotnet remove .\src\managed\GameLogic\GameLogic.csproj reference .\src\managed\Common\Common.csproj
```

## NuGet 包管理

**添加 NuGet 包**
```powershell
dotnet add .\src\managed\GameLogic\GameLogic.csproj package Newtonsoft.Json
```

**指定版本添加包**
```powershell
dotnet add .\src\managed\GameLogic\GameLogic.csproj package Newtonsoft.Json --version 13.0.3
```

**移除 NuGet 包**
```powershell
dotnet remove .\src\managed\GameLogic\GameLogic.csproj package Newtonsoft.Json
```

**列出项目依赖包**
```powershell
dotnet list .\src\managed\GameLogic\GameLogic.csproj package
```

**检查可升级的包**
```powershell
dotnet list .\src\managed\GameLogic\GameLogic.csproj package --outdated
```

**清理本地 NuGet 缓存**
```powershell
dotnet nuget locals all --clear
```

## 依赖还原

**还原当前目录下的项目或解决方案依赖**
```powershell
dotnet restore
```

**还原指定解决方案**
```powershell
dotnet restore .\XServerByAI.Managed.sln
```

**常见说明**
1. `dotnet build`、`dotnet test`、`dotnet run` 默认会隐式执行还原。
2. 在 CI 或重复执行场景下，可先单独执行 `restore`，后续搭配 `--no-restore` 提升效率。

## 构建

**构建当前目录项目**
```powershell
dotnet build
```

**构建指定解决方案**
```powershell
dotnet build .\XServerByAI.Managed.sln
```

**使用 Release 配置构建**
```powershell
dotnet build .\XServerByAI.Managed.sln -c Release
```

**跳过还原直接构建**
```powershell
dotnet build .\XServerByAI.Managed.sln -c Release --no-restore
```

**指定输出目录**
```powershell
dotnet build .\src\managed\GameLogic\GameLogic.csproj -c Release -o .\artifacts\build\GameLogic
```

## 运行

**运行当前目录项目**
```powershell
dotnet run
```

**运行指定项目**
```powershell
dotnet run --project .\src\managed\GameLogic\GameLogic.csproj
```

**使用 Release 配置运行**
```powershell
dotnet run --project .\src\managed\GameLogic\GameLogic.csproj -c Release
```

**向程序传递参数**
```powershell
dotnet run --project .\src\managed\GameLogic\GameLogic.csproj -- --config .\configs\game.json
```

**说明**
1. `--` 之后的内容会原样传给应用程序，而不是 `dotnet` CLI。
2. 如果项目不是可执行项目，例如 `classlib`，则不能直接 `dotnet run`。

## 测试

**运行当前目录测试**
```powershell
dotnet test
```

**运行指定测试项目**
```powershell
dotnet test .\src\managed\Tests\Common.Tests\Common.Tests.csproj
```

**使用 Release 配置测试**
```powershell
dotnet test .\XServerByAI.Managed.sln -c Release
```

**跳过构建直接测试**
```powershell
dotnet test .\XServerByAI.Managed.sln -c Release --no-build
```

**生成测试结果文件**
```powershell
dotnet test .\XServerByAI.Managed.sln --logger "trx;LogFileName=test-results.trx"
```

## 发布

**发布可执行项目**
```powershell
dotnet publish .\src\managed\GameLogic\GameLogic.csproj -c Release
```

**指定运行时发布**
```powershell
dotnet publish .\src\managed\GameLogic\GameLogic.csproj -c Release -r win-x64
```

**发布到指定目录**
```powershell
dotnet publish .\src\managed\GameLogic\GameLogic.csproj -c Release -o .\artifacts\publish\GameLogic
```

**自包含发布**
```powershell
dotnet publish .\src\managed\GameLogic\GameLogic.csproj -c Release -r win-x64 --self-contained true
```

**框架依赖发布**
```powershell
dotnet publish .\src\managed\GameLogic\GameLogic.csproj -c Release -r win-x64 --self-contained false
```

**说明**
1. 自包含发布会把 .NET 运行时一起带上，产物更大，但目标机器不需要预装运行时。
2. 框架依赖发布体积更小，但目标机器需要安装对应运行时。

## 清理与格式化

**清理构建输出**
```powershell
dotnet clean
```

**清理指定解决方案**
```powershell
dotnet clean .\XServerByAI.Managed.sln -c Release
```

**代码格式化**
```powershell
dotnet format .\XServerByAI.Managed.sln
```

## 打包

**将类库打成 NuGet 包**
```powershell
dotnet pack .\src\managed\Common\Common.csproj -c Release
```

**输出到指定目录**
```powershell
dotnet pack .\src\managed\Common\Common.csproj -c Release -o .\artifacts\packages
```

## 工作负载与工具

**查看已安装 workload**
```powershell
dotnet workload list
```

**安装 workload**
```powershell
dotnet workload install maui
```

**查看全局工具**
```powershell
dotnet tool list --global
```

**安装全局工具**
```powershell
dotnet tool install --global dotnet-ef
```

**更新全局工具**
```powershell
dotnet tool update --global dotnet-ef
```

## 常用参数速查

1. `-c` / `--configuration`：指定配置，如 `Debug`、`Release`。
2. `-o` / `--output`：指定输出目录。
3. `-r` / `--runtime`：指定目标运行时，如 `win-x64`、`linux-x64`。
4. `--framework`：指定目标框架，如 `net8.0`。
5. `--no-restore`：跳过依赖还原。
6. `--no-build`：跳过构建。
7. `-v` / `--verbosity`：指定日志详细程度。

## 针对当前仓库的一个参考流程

如果后续要为 `src/managed/` 补齐 .NET 解决方案与项目，可以参考以下命令顺序：

```powershell
dotnet new sln -n XServerByAI.Managed -f sln
dotnet new classlib -n Common -o .\src\managed\Common
dotnet new classlib -n GameLogic -o .\src\managed\GameLogic
dotnet sln .\XServerByAI.Managed.sln add .\src\managed\Common\Common.csproj
dotnet sln .\XServerByAI.Managed.sln add .\src\managed\GameLogic\GameLogic.csproj
dotnet add .\src\managed\GameLogic\GameLogic.csproj reference .\src\managed\Common\Common.csproj
dotnet restore .\XServerByAI.Managed.sln
dotnet build .\XServerByAI.Managed.sln -c Debug
```

## 常见问题

**1. `dotnet build` 和 `dotnet publish` 有什么区别？**  
`build` 主要用于编译，产物通常服务于本地开发与测试；`publish` 面向部署，会整理最终运行所需文件。

**2. 为什么我执行 `dotnet run` 失败？**  
常见原因是当前项目不是可执行项目，或者没有指定正确的 `.csproj` 文件。

**3. 为什么已经执行过 `restore`，后续命令还在还原？**  
因为很多命令默认会隐式还原。需要显式关闭时，可加 `--no-restore`。

**4. 什么时候应该用解决方案文件，什么时候直接对 `.csproj` 操作？**  
多项目协同时优先对 `.sln` 操作；单项目定位问题或局部构建时，直接指定 `.csproj` 更精确。
