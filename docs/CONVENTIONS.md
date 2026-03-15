# 目录结构、命名与命名空间约定

本文档定义 XServerByAI 仓库当前阶段的目录结构、命名方式与命名空间规则。除非有明确设计评审结论，否则新增模块与文档应遵循本约定。

**适用范围**
1. 适用于仓库顶层目录、`src/native`、`src/managed`、`docs`、`configs`。
2. 适用于 CMake 目标名、.NET 项目名、源码文件名、C++ 命名空间与 C# 命名空间。
3. 适用于当前单机多进程架构骨架阶段，后续扩展也应保持兼容，避免无理由重命名。

**仓库顶层目录**
1. `cmake/`：放置 CMake 公共模块、工具链文件与构建辅助脚本。
2. `configs/`：放置 GM、Gate、Game 等进程的运行配置文件。
3. `docs/`：放置长期维护文档与里程碑设计说明。
4. `src/`：放置所有源码、项目文件与与源码强关联的构建描述。
5. `logs/` 属于运行期生成目录，默认用于日志输出，不应提交到仓库。
6. 顶层新增目录必须满足“无法归入现有目录”的前提，并同步更新 `docs/PROJECT.md` 与本规范。

**源码目录约定**
1. `src/native/` 仅放置 C++ 运行时、网络、宿主与进程入口相关代码。
2. `src/managed/` 仅放置 C# 业务逻辑、共享契约与后续测试项目。
3. 不在 `src/` 根目录直接堆放业务源码；必须先归属到 `native` 或 `managed` 的明确子目录。
4. 新增模块时优先放入现有子树；只有在职责边界清晰时才新增新的一级子目录。

**Native 目录与命名规则**
1. `src/native` 下的库模块目录使用全小写名称，必要时使用 `lower_snake_case`，例如 `core`、`net`、`ipc`、`host`。
2. 统一节点入口固定放在 `src/native/node/`，生成单个 `xserver-node` 可执行文件。
3. `src/native/gm`、`src/native/gate`、`src/native/game` 可用于承载进程专属实现代码，但不再各自生成独立可执行目标。
4. 每个 native 子目录都应有独立的 `CMakeLists.txt`；可执行目标的文件名与输出名可以按职责命名，不强制与目录同名。
5. 统一节点入口文件采用 `node_main.cpp` 命名；占位或模块聚合实现文件可使用与目录同名的文件，例如 `core.cpp`、`net.cpp`。
6. 当模块进入类型化实现阶段，类或结构体主文件应优先采用 `PascalCase` 文件名，并与主类型同名。

**Managed 目录与命名规则**
1. `src/managed` 下的项目目录使用 `PascalCase`，目录名、项目名与主程序集名保持一致语义，例如 `Foundation`、`GameLogic`。
2. `.csproj` 文件名默认与项目目录同名，例如 `Foundation/Foundation.csproj`。
3. 共享类库与业务类库保持单向依赖；底层公共项目不得反向引用业务项目。
4. 后续测试项目统一放在 `src/managed/Tests/` 下，并采用 `<ProjectName>.Tests` 命名。
5. 一个源码文件应只承载一个主公开类型，文件名与主类型名保持一致。
6. 临时脚本、生成代码与第三方代码不得直接混入业务项目根目录，应使用明确的子目录隔离。

**文档命名规则**
1. 里程碑设计说明使用 `<FeatureId>.md` 命名，例如 `M1-05.md`。
2. 长期维护的规范或说明文档使用全大写英文命名，必要时可用下划线分词，例如 `PROJECT.md`、`DEVELOPMENT_PLAN.md`、`DOTNET_CLI.md`。
3. 新增长期维护文档时，应优先复用已有主题文档；只有内容边界清晰时才新增新文档。

**配置文件命名规则**
1. 正式运行配置统一使用单个 UTF-8 JSON 文件与 `.json` 扩展名，不并行维护 YAML 正式版本。
2. 配置文件名由部署方决定，推荐放在 `configs/` 下，例如 `configs/config.json` 或 `configs/local-dev.json`。
3. `gm`、`gate0`、`game0` 这类实例选择器存在于配置文件内部，作为顶层 `gm` 块或 `gate` / `game` 子键，不再把实例拆成多个文件。
4. `logs/` 为运行期输出目录，不得作为配置模板或手工维护资源目录使用；日志文件命名与滚动细则见 `docs/CONFIG_LOGGING.md`。

**C++ 命名空间规则**
1. 所有项目内 C++ 代码以 `xs` 作为根命名空间。
2. 第二层命名空间默认与模块目录对应，例如 `xs::core`、`xs::net`、`xs::ipc`、`xs::host`。
3. 进程内部专用类型可使用 `xs::gm`、`xs::gate`、`xs::game` 等子命名空间。
4. 除进程入口 `main` 与必须暴露给外部工具链的符号外，不应向全局命名空间暴露项目类型。
5. 新增更细分的命名空间时，继续使用小写或 `lower_snake_case`，避免混入 `PascalCase` 风格。

**C# 命名空间规则**
1. 所有 managed 项目使用 `XServer.Managed` 作为根命名空间前缀。
2. 项目默认命名空间与程序集名保持一致，例如 `XServer.Managed.Foundation`、`XServer.Managed.GameLogic`。
3. 子目录与子命名空间保持一一映射，例如 `src/managed/GameLogic/Space/SpaceService.cs` 对应 `XServer.Managed.GameLogic.Space`。
4. C# 类型名使用 `PascalCase`，接口使用 `I` 前缀，私有字段使用 `_camelCase`。
5. 不要在一个项目内混用多个根命名空间；跨项目共享能力应通过项目引用而不是复制命名空间实现。

**新增模块时的同步要求**
1. 新增 native 模块时，需要同步更新 `src/native/CMakeLists.txt` 与相关里程碑文档。
2. 新增 managed 项目时，需要同步更新 `.sln`、项目引用关系与相关文档。
3. 新增顶层目录或改变目录职责时，需要同步更新 `docs/PROJECT.md` 与本规范。
4. 如果命名规则发生例外，必须在设计说明中记录原因，而不是直接打破现有模式。
