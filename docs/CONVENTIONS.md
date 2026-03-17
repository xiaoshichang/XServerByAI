# 目录结构、命名与命名空间约定

本文档定义 XServerByAI 仓库当前阶段的目录结构、命名方式与命名空间规则。除非有明确设计评审结论，否则新增模块与文档应遵循本约定。

**适用范围**
1. 适用于仓库顶层目录、`3rd`、`src/native`、`src/managed`、`docs`、`configs`。
2. 适用于 CMake 目标名、.NET 项目名、源码文件名、C++ 命名空间与 C# 命名空间。
3. 适用于当前多进程架构骨架阶段；开发期默认以单机多进程形态联调，后续扩展到多机部署时也应保持兼容，避免无理由重命名。

**仓库顶层目录**
1. `cmake/`：放置 CMake 公共模块、工具链文件与构建辅助脚本。
2. `configs/`：放置 GM、Gate、Game 等进程的运行配置文件。
3. `docs/`：放置长期维护文档与里程碑设计说明。
4. `src/`：放置所有源码、项目文件与与源码强关联的构建描述。
5. `3rd/`：放置 vendored 第三方依赖；每个依赖单独使用子目录，保存上游源码与必要的项目侧构建包装。
6. `logs/`：运行期生成目录，默认用于日志输出，不应提交到仓库。
7. 顶层新增目录必须满足“无法归入现有目录”的前提，并同步更新 `docs/PROJECT.md`、本规范以及相关依赖说明。

**源码目录约定**
1. `src/native/` 仅放置 C++ 运行时、网络、宿主与进程入口相关代码。
2. `src/managed/` 仅放置 C# 业务逻辑、共享契约与后续测试项目。
3. 不在 `src/` 根目录直接堆放业务源码；必须先归属到 `native` 或 `managed` 的明确子目录。
4. 临时脚本、生成代码与第三方代码不得混入 `src/`；第三方源码统一进入 `3rd/<libname>/`。

**Native 目录与命名规则**
1. `src/native` 下的库模块目录使用全小写名称，必要时使用 `lower_snake_case`，例如 `core`、`net`、`ipc`、`host`。
2. 统一节点入口固定放在 `src/native/node/`，生成单个 `xserver-node` 可执行文件。
3. `src/native/gm`、`src/native/gate`、`src/native/game` 可用于承载进程专属实现代码，但不再各自生成独立可执行目标。
4. 每个 native 子目录都应有独立的 `CMakeLists.txt`；可执行目标的文件名与输出名可以按职责命名，不强制与目录同名。
5. 统一入口文件采用 `node_main.cpp` 命名；占位或模块聚合实现文件可使用与目录同名的文件，例如 `core.cpp`、`net.cpp`。
6. 当模块进入类型化实现阶段，类或结构体主文件优先采用 `PascalCase` 文件名，并与主类型同名。

**Third-Party 目录约定**
1. `3rd/` 下每个依赖都应使用独立子目录，例如 `3rd/spdlog/`、`3rd/zeromq/`。
2. 每个依赖目录应包含上游源码；若项目需要额外包装脚本或说明，应放在该依赖目录或 `3rd/CMakeLists.txt` / `3rd/README.md` 中集中管理。
3. 业务代码不得直接依赖上游 target 命名；项目内部统一通过项目侧别名或包装层引用第三方库。
4. 升级第三方版本时，必须同步更新 `3rd/README.md` 与相关开发计划/规范文档。

**Managed 目录与命名规则**
1. `src/managed` 下的项目目录使用 `PascalCase`，目录名、项目名与主程序集名保持一致语义，例如 `Foundation`、`GameLogic`。
2. `.csproj` 文件名默认与项目目录同名，例如 `Foundation/Foundation.csproj`。
3. 共享类库与业务类库保持单向依赖；底层公共项目不得反向引用业务项目。
4. 后续测试项目统一放在 `src/managed/Tests/` 下，并采用 `<ProjectName>.Tests` 命名。
5. 一个源文件应只承载一个主公开类型，文件名与主类型名保持一致。
6. 临时脚本、生成代码与第三方代码不得直接混入 managed 业务项目根目录。

**文档命名规则**
1. 里程碑设计说明使用 `<FeatureId>.md` 命名，例如 `M1-05.md`、`M1-13.md`。
2. 长期维护的规范或说明文档使用全大写英文命名，必要时用下划线分词，例如 `PROJECT.md`、`DEVELOPMENT_PLAN.md`、`CONFIG_LOGGING.md`。
3. 新增长期维护文档时，应优先复用已有主题文档；只有内容边界清晰时才新增新文档。

**配置文件命名规则**
1. 正式运行配置统一使用单个 UTF-8 JSON 文件与 `.json` 扩展名，不并行维护 YAML 正式版本。
2. 配置文件名由部署方决定，推荐放在 `configs/` 下，例如 `configs/config.json` 或 `configs/local-dev.json`。
3. `gm`、`gate0`、`game0` 这类实例选择器存在于配置文件内部，不将实例拆成多份独立配置文件。
4. `logs/` 为运行期输出目录，不得作为配置模板或手工维护资源目录使用；日志文件命名与滚动细则见 `docs/CONFIG_LOGGING.md`。

**C++ 命名空间规则**
1. 所有项目内 C++ 代码以 `xs` 作为根命名空间。
2. 第二层命名空间默认与模块目录对应，例如 `xs::core`、`xs::net`、`xs::ipc`、`xs::host`。
3. 进程内专用类型可使用 `xs::gm`、`xs::gate`、`xs::game` 等子命名空间。
4. 除进程入口 `main` 与必须暴露给外部工具链的符号外，不应向全局命名空间暴露项目类型。
5. 新增更细粒度的命名空间时，继续使用小写或 `lower_snake_case`，避免混入 `PascalCase`。

**代码块大括号风格**
1. C++ 与 C# 代码统一采用 Allman 风格，代码块的左花括号 `{` 必须另起一行。
2. 该约束仅适用于项目自有源码与测试代码，例如 `src/`、`tests/` 下的文件；`3rd/` 中 vendored 第三方代码不要求按此规则调整，也不应为此做样式改写。
3. 该约束适用于命名空间、类、结构体、枚举、函数以及 `if` / `else` / `for` / `while` / `switch` / `try` / `catch` 等块级语句。
4. C++ 指针与引用采用贴近类型的写法，统一写成 `T* value`、`T& value`、`T& operator=(const T&) = delete;`，不写成 `T *value`、`T &value` 或 `T &operator=(const T &) = delete;`。
5. 新增代码与修改过的既有代码都应按此规则整理，避免在同一文件内混用同行大括号与另起一行两种写法，以及混用不同的指针/引用对齐风格。

**C# 命名空间规则**
1. 所有 managed 项目使用 `XServer.Managed` 作为根命名空间前缀。
2. 项目默认命名空间与程序集名保持一致，例如 `XServer.Managed.Foundation`、`XServer.Managed.GameLogic`。
3. 子目录与子命名空间保持一一映射，例如 `src/managed/GameLogic/Space/SpaceService.cs` 对应 `XServer.Managed.GameLogic.Space`。
4. C# 类型名使用 `PascalCase`，接口使用 `I` 前缀，私有字段使用 `_camelCase`。
5. 不要在一个项目内混用多个根命名空间；跨项目共享能力应通过项目引用实现。

**新增模块时的同步要求**
1. 新增 native 模块时，需要同步更新 `src/native/CMakeLists.txt` 与相关里程碑文档。
2. 新增 managed 项目时，需要同步更新 `.sln`、项目引用关系与相关文档。
3. 新增顶层目录或改变目录职责时，需要同步更新 `docs/PROJECT.md`、本规范以及相关依赖说明（如 `3rd/README.md`）。
4. 如果命名规则发生例外，必须在设计说明中记录原因，而不是直接打破现有模式。