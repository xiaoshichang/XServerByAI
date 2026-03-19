# M3-02 测试记录

## 范围

- 分支：`M3-02`
- 开发提交：
  - `2f9a3e8`（`developer: M3-02 gm control listener lifecycle`）
  - `e0b610c`（`developer: refactor node architecture around ServerNode`）
- 设计依据：`docs/M3-02.md`

## 执行结果

- 依赖检查通过：`M3-01`、`M2-08` 已完成，`M3-02` 当前仍为 `开发中`
- Native 全量验证已执行并通过：
  - `cmake -S . -B build -DXS_BUILD_TESTS=ON`
  - `cmake --build build --config Debug`
  - `ctest --test-dir build -C Debug --output-on-failure`
- 本次变更未触及 `XServerByAI.Managed.sln`、`src/managed/` 或其他 .NET 工程文件，因此未执行 managed build
- `xs_node_runtime_tests` 与 `xs_node_gm_node_tests` 均通过，说明：
  - `ServerNode` 生命周期调度与 `Uninit()` 回收路径已接入 `RunNodeProcess(...)`
  - `GmNode` 能基于 GM 配置启动内部被动监听
  - `InnerNetwork` 的 wildcard bind、收包、状态收敛与日志输出具备基本行为

## 问题清单

### 1. 重要问题：`RunNodeProcess(...)` 没有按设计对 Init/Run 阶段错误做统一阶段映射

- 设计要求：`docs/M3-02.md` 明确要求运行时错误按“创建 / 初始化 / 运行”三个阶段分别映射，便于定位节点生命周期问题。
- 当前实现：
  - 工厂抛异常或返回空节点时，运行时会统一映射为 `NodeCreateFailed`
  - 但 `server_node->Init()` / `server_node->Run()` 只在“抛异常”时映射为 `NodeInitFailed` / `NodeRunFailed`
  - 如果节点方法正常返回非零错误码（例如 `InvalidArgument`），`RunNodeProcess(...)` 会直接把该原始错误码返回给调用方
- 影响：
  - 调用方无法仅通过 runtime error code 区分“参数/配置层错误”与“节点初始化阶段错误”
  - `RunNodeProcess(...)` 的阶段边界不稳定：相同 Init 失败，抛异常时得到 `NodeInitFailed`，显式返回错误码时却得到其他枚举值
  - 这与设计中“按创建、初始化、运行三个阶段分别映射”的契约不一致
- 现有测试同样固化了该行为：`tests/cpp/node_runtime_tests.cpp` 当前显式断言节点 `Init()` 返回 `InvalidArgument` 时，`RunNodeProcess(...)` 也返回 `InvalidArgument`，因此该问题不会被当前 CI 识别

## 结论

- `M3-02` 本轮测试未通过。
- `docs/DEVELOPMENT_PLAN.md` 应保持 `开发中`，不应提交 `[Feature Passed]` 状态变更。
- 需要先统一 `RunNodeProcess(...)` 对节点创建、初始化、运行三个阶段的错误映射，并同步修正对应测试断言后，再重新执行 native 全量验证。
