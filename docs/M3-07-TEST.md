# M3-07 测试记录

## 验证范围

- `gm.controlNetwork.listenEndpoint` 配置解析与 GM 节点配置选择
- `GM` 本地 HTTP 管理接口与同一 `MainEventLoop` / `io_context` 的协作行为
- `GET /healthz`、`GET /status`、`POST /shutdown` 三个基础路由
- 既有 `GM` 注册、心跳、Gate 占位网络与节点创建路径回归

## 执行命令

- `cmd /v:on /c "set XS_OLDPATH=%PATH%&& set PATH=& set Path=!XS_OLDPATH!&& C:\WINDOWS\System32\WindowsPowerShell\v1.0\powershell.exe -ExecutionPolicy Bypass -File .agents\skills\test-feature\scripts\build-native.ps1"`
- `build-native.ps1` 内部执行 `cmake -S . -B build -DXS_BUILD_TESTS=ON`、`cmake --build build --config Debug` 与 `ctest --test-dir build -C Debug --output-on-failure` 全量 native 验证流程。

## 结果

- 全量 native Debug 构建通过。
- `ctest` 共执行 17 项测试，全部通过。
- 关键覆盖包括：
  - `xs_core_json_tests`：验证 `gm.controlNetwork.listenEndpoint` 成功加载，并拒绝缺失 `gm.controlNetwork` 的配置。
  - `xs_node_gm_node_tests`：验证 `GM` HTTP 管理接口可返回 `/healthz`、`/status`，并由 `/shutdown` 驱动节点正常退出。
  - `xs_node_create_helper_tests`、`xs_node_gate_node_tests`、`xs_node_gm_inner_service_tests`：验证配置 schema 扩展后，既有节点创建、Gate 客户端网络占位和 GM Inner 服务路径未回归。

## 备注

- 当前 Windows/MSBuild 环境同时暴露了 `Path` 和 `PATH` 两个环境变量键，直接执行 `build-native.ps1` 内部的 `cmake --build` 会触发工具链环境冲突；本次测试通过 `cmd /v:on /c "set XS_OLDPATH=%PATH%&& set PATH=& set Path=!XS_OLDPATH!&& ..."` 在子进程中归一化 PATH 后执行原始技能脚本，不影响仓库代码行为。
