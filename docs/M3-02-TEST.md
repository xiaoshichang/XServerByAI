# M3-02 测试记录

## 范围

- 分支：`M3-02`
- 开发提交：
  - `ba7cbf6`：`developer: fix node runtime stage error mapping`
  - `82afeb6`：`developer: simplify node lifecycle architecture`
- 设计依据：`docs/M3-02.md`
- 验证目标：确认 `NodeCreateHelper + ServerNode` 生命周期骨架、GM `InnerNetwork` 监听、Gate/Game 占位节点入口与节点日志输出满足 M3-02 设计。

## 复测结果

- 依赖检查通过：`M3-01`、`M2-08` 已完成，复测前 `M3-02` 状态为 `开发中`
- Native 全量验证已执行并通过：
  - `cmake -S . -B build -DXS_BUILD_TESTS=ON`
  - `cmake --build build --config Debug`
  - `ctest --test-dir build -C Debug --output-on-failure`
  - 结果：`13/13` 测试通过
- 本次开发提交未触及 `XServerByAI.Managed.sln`、`src/managed/` 或其他 .NET 工程文件，因此未执行 managed build
- 自动化测试覆盖通过：
  - `xs_node_create_helper_tests` 验证 CLI 解析、节点构造、生命周期成功/失败路径与清理行为
  - `xs_node_gm_node_tests` 验证 GM 配置约束、`InnerNetwork` 被动监听、收包、状态收敛与事件循环内运行
- 代码与行为检查结果：
  - `NodeCreateHelper` 已接管命令行解析与节点选择，不再依赖额外 runtime 包装层
  - `ServerNode` 统一承载 `Init()`、`Run()`、`Uninit()` 生命周期，配置、logger、mainloop 与错误文本均在节点自身边界内管理
  - `GmNode` 通过 `InnerNetwork` 建立 GM 内部监听；`GateNode` / `GameNode` 保持可启动的占位骨架，符合当前设计范围
- 可执行程序 smoke 验证通过：
  - 缺少参数时进程退出码为 `1`，并输出 usage
  - 非法 nodeId 时进程退出码为 `1`，并返回 nodeId 校验错误
  - `Gate0` 启动后退出码为 `0`
  - `Game0` 启动后退出码为 `0`
  - `GM` 启动后保持监听运行，手动停止前持续存活，符合 GM Inner 网络监听节点预期
  - 已生成角色日志：
    - `build/test-output/m3-02-smoke/logs/root/GM-2026-03-19.log`
    - `build/test-output/m3-02-smoke/logs/gate/Gate0-2026-03-19.log`
    - `build/test-output/m3-02-smoke/logs/game/Game0-2026-03-19.log`

## 最新结论

- `M3-02` 本轮复测通过。
- `docs/DEVELOPMENT_PLAN.md` 已将 `M3-02` 从 `开发中` 更新为 `已完成`。
- `GateNode` / `GameNode` 当前仍是占位骨架，但这属于 `docs/M3-02.md` 明确约束的交付范围；后续真实集群与客户端行为继续由 `M3-08`、`M3-10`、`M4-02` 等条目承接。


