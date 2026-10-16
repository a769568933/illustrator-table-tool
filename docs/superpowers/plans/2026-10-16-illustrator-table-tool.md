# Illustrator Table Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. 本计划推荐在当前对话直接实施；尚待用户审阅并选择执行方式。

**Goal:** 为 Windows x64 Illustrator 28.5 交付经过加载与交互验证的原生画布表格插件，并通过公开 GitHub 仓库的 Actions 编译生成安装包。

**Architecture:** C++ 表格核心负责几何和结构，Illustrator SDK 适配负责工具事件、普通对象组、原生文字框及文档字典。画布辅助绘制只显示选区和拖动预览；成功提交的编辑统一进入 Illustrator 撤销栈。

**Tech Stack:** C++17、Adobe Illustrator 2024 Windows C++ SDK、CMake、GitHub Actions `windows-2022`。编译器版本与插件资源处理以取得的 2024 SDK 的 Windows 样例工程要求为准。

**Spec:** `docs/superpowers/specs/2026-10-16-illustrator-table-tool-design.md`

## Global Constraints

- Windows 64 位 Adobe Illustrator 2024，版本 28.5。
- 首版允许 1–100 行、1–50 列，总格数不超过 2,000。
- 所有尺寸必须为正数，最小行高和列宽为 2 毫米。
- 首版保证未旋转、未倾斜的表格可编辑。
- 首版文字竖向排版采用顶部对齐。
- 用户允许使用 GitHub 云端编译，并明确要求新建公开仓库。
- SDK 不提交公开仓库。
- 没有 SDK 时仅允许核心测试成功；原生构建必须明确失败并说明缺失项，不能上传占位 `.aip`。
- 云端构建不能替代 Illustrator 28.5 本机加载和交互验收。
- 未经核实，不猜测 SDK 函数签名，不用桩接口或替代网页声称原生功能已经完成。

## Review Focus

1. 中文输入法处于组字阶段：切换表格选择不得截获输入或丢失未提交文字；在 Task 3 实测。
2. 合并区横跨待删除或插入的行列：结构保持完整，文字保留；在 Task 4 自动测试。
3. 用户用原生文字工具改字后拖动边线：不得被缓存内容覆盖；在 Task 5 实测。
4. 整表复制或轴向缩放后再次编辑：复制实例独立，命中与新尺寸一致；在 Task 5 实测。
5. 操作中切换文档、锁定对象或取消拖动：不误改另一个文档，也不留下半成品；在 Task 3、5 测试。

## File Structure

```text
CMakeLists.txt                         核心、测试和插件构建入口
cmake/IllustratorSdk.cmake              SDK 查找、版本检查与构建依赖
include/table/Types.hpp                几何、样式、单元格和错误类型
include/table/TableModel.hpp           表格结构与尺寸操作接口
include/table/Geometry.hpp             命中、选区与共享边线接口
include/table/Interaction.hpp          可取消的选择与拖动状态机
include/table/Serialization.hpp        模型版本和序列化接口
src/core/TableModel.cpp                结构、文本保留和尺寸校验
src/core/Geometry.cpp                  坐标及可见边线计算
src/core/Interaction.cpp               无宿主依赖的交互逻辑
src/core/Serialization.cpp             带版本和限额的持久模型格式
src/plugin/Plugin.cpp                  SDK 入口、suite 生命周期和消息路由
src/plugin/ToolController.cpp          工具事件、光标与状态切换
src/plugin/DocumentAdapter.cpp         文档对象、字典、文字及事务
src/plugin/AnnotationRenderer.cpp      辅助选区、预览和溢出提示
src/plugin/NativeTextEditor.cpp        原生文字编辑接入
src/plugin/Dialogs.cpp                 中文参数和样式对话框
resources/                            插件资源、光标及版本信息
tests/core_tests.cpp                   核心可执行测试入口
tests/model_tests.cpp                  结构、文本和持久化测试
tests/geometry_tests.cpp               几何和命中测试
tests/interaction_tests.cpp            拖动、选择和取消测试
scripts/build.ps1                     本地及 Actions 共用的编译入口
scripts/package.ps1                   产物核验、清单、摘要和 ZIP
.github/workflows/build.yml           核心测试与 SDK 插件构建
docs/SDK_SETUP.md                     官方 SDK 获取与构建配置
docs/HOST_ACCEPTANCE.md               28.5 实机测试步骤及真实结果
README.md                            状态、安装、使用和卸载
```

原生 SDK 的资源扩展名、公共辅助源码和具体工程设置，在 Task 1 读取官方样例后固定并更新该文件列表；不要提前伪造 PiPL 或入口 ABI。

## Task 1: 取得并验证 SDK，建立可加载的云端原生工程

**Files:** `CMakeLists.txt`、`cmake/IllustratorSdk.cmake`、`src/plugin/Plugin.cpp`、`resources/`、`scripts/build.ps1`、`.github/workflows/build.yml`、`docs/SDK_SETUP.md`、`README.md`。

**Interfaces:**
- Consumes: 官方 Illustrator 2024 Windows SDK 的本地路径；云端使用 Secret `ILLUSTRATOR_SDK_URL` 与 `ILLUSTRATOR_SDK_SHA256`。
- Produces: `scripts/build.ps1 -SdkRoot <path> -Configuration Release`；输出 `build/plugin/Release/IllustratorTableTool.aip`。CMake 选项 `TABLE_BUILD_PLUGIN` 默认 `ON`，关闭该选项只构建无宿主核心测试。
- Plugin SDK 入口、suite 获取释放和资源生成严格沿用已核实的对应版本样例。

- [ ] 记录 SDK 压缩包来源、SHA-256、版本、样例 Windows 工具集及公共库要求。若用户尚未登录官方入口，等待用户完成登录并提供文件位置；此时可准备计划，但不编造宿主适配。
- [ ] 核验插件关闭时可配置核心工程，插件开启而 SDK 缺失时配置必须失败，说明应设置的 SDK 路径。
- [ ] 阅读 2024 SDK 的最小工具与 Annotator 示例，建立只有初始化、关闭和测试菜单命令的真实 `.aip` 工程。
- [ ] 本地初始化 Git，将文档与源码提交到 `main`。在已连接账户 `a769568933` 下创建用户授权的公开仓库 `illustrator-table-tool`；若名字已被占用先检查现有仓库归属和内容，不覆盖已有项目。
- [ ] 核心测试在普通提交和拉取请求运行；需要 SDK Secret 的插件构建仅在受信任的 `main` 提交或手动触发时运行。下载 SDK 后验证 SHA-256，再解压，不打印下载凭据。
- [ ] 读取云端构建结果与日志，修复至生成真实 x64 `.aip`。检查插件 PE 架构、导出入口和资源，不允许仅靠改后缀生成产物。
- [ ] 取得构建产物，在 Illustrator 28.5 手动或通过可用的原生应用工具加载。确认菜单出现、执行测试命令成功、退出后无崩溃；写入真实结果。
- [ ] 提交：`build: add verified Illustrator 2024 native plugin pipeline`。

**Verification:** 云端 Windows 编译成功、产物真实、本机 28.5 加载成功。这是进入完整原生功能开发的前提。SDK 未取得时本任务保持未完成。

## Task 2: 创建模型、几何和原生表格对象

**Files:** `include/table/Types.hpp`、`include/table/TableModel.hpp`、`include/table/Geometry.hpp`、对应 `src/core/`、`src/plugin/DocumentAdapter.cpp`、`src/plugin/Dialogs.cpp`、`tests/model_tests.cpp`、`tests/geometry_tests.cpp`。

**Interfaces:**
- `using CellId = std::uint64_t;`
- `struct Point { double x; double y; };`、`struct Rect { double left; double top; double right; double bottom; };`；核心坐标 x 向右、y 向下，宿主适配负责 AI 坐标转换。
- `TableModel::create(int rows, int columns, double widthPt, double heightPt) -> TableModel`；不合法参数抛出 `ValidationError`，不返回部分模型。
- `TableModel::cellAt(int row, int column) const -> const Cell&`；返回覆盖该坐标的锚点单元格。
- `TableModel::setText(CellId id, const std::u16string& text) -> void`。
- `Geometry::cellBounds(const TableModel&, CellId) -> Rect`。
- `Geometry::visibleEdges(const TableModel&) -> std::vector<Edge>`；每条共享边仅一次。
- `DocumentAdapter::createTable(const TableModel&, Point topLeft) -> TableRef`；`TableRef` 包含宿主对象句柄与文档身份，不能跨文档复用。

- [ ] 先写并运行失败测试：4×4 默认表格；100×20 合法而 100×21 拒绝；零行、51 列、负数、NaN、无穷和不足每格 2 毫米的尺寸全部拒绝。
- [ ] 实现等分尺寸、稳定单元格编号和内容映射。使用 `72.0 / 25.4` 换算毫米。
- [ ] 先写并运行失败测试：2×2 表格共有 12 条不重复的单格边段；边段坐标匹配单元格几何；合并后的隐藏边不进入结果。
- [ ] 实现纯核心几何并通过 `cmake -S . -B build/core -DTABLE_BUILD_PLUGIN=OFF`、`cmake --build build/core --config Release`、`ctest --test-dir build/core -C Release --output-on-failure`。
- [ ] 实现中文新建对话框与画布单击放置；生成普通组、填色路径、共享边线、外框及原生区域文字框。按文档模式使用 RGB/CMYK。
- [ ] 验证取消新建不改文档，失败时删除新建的不完整组；创建 2×2 表格后用原生文字工具确认每格文字可编辑。
- [ ] 提交：`feat: create editable native table artwork`。

## Task 3: 画布选择、边线拖动与原生文字编辑闭环

**Files:** `include/table/Interaction.hpp`、`src/core/Interaction.cpp`、`src/plugin/ToolController.cpp`、`src/plugin/AnnotationRenderer.cpp`、`src/plugin/NativeTextEditor.cpp`、`tests/interaction_tests.cpp`、`docs/HOST_ACCEPTANCE.md`。

**Interfaces:**
- `Geometry::hitTest(const TableModel&, Point localPoint, double zoom, double tolerancePx) -> HitResult`，`HitResult` 明确区分单元格、可见边段和表格外部。
- `Interaction::pointerDown(const TableModel&, Point, bool shift, double zoom) -> void`。
- `Interaction::pointerMove(Point) -> void`；`Interaction::pointerUp() -> std::optional<TableModel>`；`Interaction::cancel() -> void`。操作原模型的副本，取消返回原状态。
- `NativeTextEditor::beginEdit(TableRef, CellId) -> bool`，只在成功进入真实画布文字编辑时返回 `true`。
- `DocumentAdapter::applyGeometry(TableRef, const TableModel&) -> void`，一次拖动结束只提交一次，预览只走辅助绘制。

- [ ] 失败测试覆盖：单击选格、Shift 矩形选区、表外清除、隐藏边不可命中；25%、100%、400% 缩放下相同屏幕容差命中一致。
- [ ] 失败测试覆盖：内部竖线只改变相邻两列且总宽不变；外部右边线改变总宽；横线同理；最小尺寸夹取；Esc 后模型完全不变。
- [ ] 实现命中和拖动状态机，运行全部核心测试。
- [ ] 注册工具事件、边线光标、辅助高亮和预览；辅助内容不得写入文档路径。文档切换或对象失效时取消当前操作。
- [ ] 按 SDK 核实的方法接入原生文字编辑。双击后实测微软拼音组字、候选确认、中文换行、复制粘贴及中英文混输；不使用自制字符捕获代替文字引擎。
- [ ] 实测拖动撤销一次即回到拖动前状态，Esc 不新增撤销内容，选区不进入 PDF 导出。
- [ ] 若原生双击文字编辑无法可靠完成，记录日志和实际表现，先解决或与用户修订设计；不进入“完整表格功能已完成”的状态。
- [ ] 提交：`feat: edit table cells and resize tracks on canvas`。

## Task 4: 行列结构与合并拆分

**Files:** `src/core/TableModel.cpp`、`src/core/Geometry.cpp`、`src/plugin/ToolController.cpp`、`tests/model_tests.cpp`。

**Interfaces:**
- `struct CellRange { int firstRow; int firstColumn; int lastRow; int lastColumn; };`，闭区间，构造时规范化方向。
- `TableModel::merge(CellRange) -> void`、`TableModel::split(CellId) -> void`。
- `TableModel::insertRows(int before, int count) -> void`、`insertColumns(int before, int count) -> void`。
- `TableModel::eraseRows(int first, int count) -> void`、`eraseColumns(int first, int count) -> void`。
- 所有结构操作先在候选模型验证，再提交；失败保持原模型内容和编号不变。

- [ ] 先写并运行失败测试：矩形合并按行优先连接文字；部分切开已有合并区拒绝；未编辑的合并格拆分还原原内容；已编辑的合并格拆分按设计保留新旧内容。
- [ ] 先写并运行失败测试：跨合并区插入扩展跨度；删除锚点时迁移内容到有效锚点；删除部分跨度后仍能正确拆分；禁止删光、越界和超过总格数限额。
- [ ] 实现上述结构算法及对应菜单命令；每次菜单操作前同步真实文字。
- [ ] 运行全部核心测试，在 AI 中验证 3×3 表格合并中间区域后反复插入、删除和撤销，检查边线无重复、无丢字。
- [ ] 提交：`feat: add table structure and merged cell editing`。

## Task 5: 文档持久化、文字同步和日常编辑

**Files:** `include/table/Serialization.hpp`、`src/core/Serialization.cpp`、`src/plugin/DocumentAdapter.cpp`、`src/plugin/Plugin.cpp`、`tests/model_tests.cpp`、`docs/HOST_ACCEPTANCE.md`。

**Interfaces:**
- `serialize(const TableModel&) -> std::string`；`deserialize(std::string_view) -> TableModel`，UTF-8、版本 `1`、严格校验结构和数量限额。
- `DocumentAdapter::readTable(TableRef) -> TableModel`，以文档实际文字框内容覆盖缓存文本，读取实际外框同步位置和轴向尺寸。
- `DocumentAdapter::commit(TableRef, const TableModel&, const std::u16string& undoLabel) -> void`，更新对象及持久字典，失败恢复旧状态。
- `DocumentAdapter::detach(TableRef) -> void`，移除插件元数据，保留可见文字与路径。

- [ ] 失败测试覆盖：模型和中文文字序列化往返一致；未知版本、损坏跨度、重复 ID、超限输入均拒绝；失败不生成半个模型。
- [ ] 实现格式与宿主字典读写，不通过执行脚本解析模型数据。复制实例的运行时身份以实际对象句柄和当前文档为准，不只依赖复制过来的字符串 ID。
- [ ] 在 AI 保存 `.ai`、关闭、重开，验证合并、文字及行列仍可编辑。
- [ ] 在原生文字工具中修改文字后拖动边线，确认文字及已有局部文字格式保留；几何更新调整文字框范围，不重建不必要的文字对象。
- [ ] 复制两张表，分别编辑；移动和轴向缩放后验证命中；旋转或倾斜后拒绝结构编辑并显示原因。对象锁定、隐藏或关联对象缺失时不修改。
- [ ] 拖动中切文档或取消，确认原文档和新文档都无误改；增删、合并、拆分及文字同步后分别撤销重做。
- [ ] 实测转换普通对象后可继续使用原生工具编辑，但表格工具不再将其识别为结构化表格。
- [ ] 提交：`feat: persist tables and preserve native document edits`。

## Task 6: 基础样式、打包与完整验收

**Files:** `src/plugin/Dialogs.cpp`、`src/plugin/DocumentAdapter.cpp`、`scripts/package.ps1`、`.github/workflows/build.yml`、`README.md`、`docs/HOST_ACCEPTANCE.md`。

**Interfaces:**
- `DocumentAdapter::applyStyle(TableRef, CellRange, const CellStyle&) -> void`；`CellStyle` 包含底色、边框、文字颜色、字号、字体、水平对齐及上下内边距，默认顶部对齐。
- `scripts/package.ps1 -PluginPath <aip> -OutputDirectory <directory>`，检查输入存在且是 x64 PE 后生成包含插件、说明及 `SHA256SUMS.txt` 的 ZIP。

- [ ] 实现中文样式对话框，对所选范围应用样式。取消不修改文档，缺失字体给出可操作说明而不无提示替换。
- [ ] 在 RGB 和 CMYK 文档分别验证颜色、中文字体、边框和顶部对齐；长文字显示辅助溢出提示，不丢字、不自动改字号。
- [ ] 对打包流程验证：缺失插件、文本伪装 `.aip`、架构错误时失败；成功 ZIP 不含 SDK 或凭据。
- [ ] 云端全量编译与测试后下载最终产物，在 28.5 执行验收表：创建、选择、拖线、中文编辑、合并拆分、增删行列、保存重开、复制缩放、撤销重做、样式及导出。
- [ ] README 写明安装位置、重启、工具入口、首次使用、版本限制与卸载方法。检查实际产物名称和说明一致。
- [ ] 记录每项验收日期、版本、结果和失败原因；任何未验证项目保持“未验证”，不写成通过。
- [ ] 提交并交付：公开源码链接、成功构建链接、ZIP、SHA-256 与实机验收记录。

## Execution Handoff

- 已确认：用户回复“开始吧”，设计获认可。
- 当前实施建议：由本对话直接执行上述任务，避免在 SDK 接入尚未确认前拆成多个并行工作。
- 尚需用户审阅本计划并确认执行方式。
- 外部资源：官方 Illustrator 2024 Windows C++ SDK 尚未取得；没有该资源就无法完成 Task 1 的原生编译与宿主加载。后续实施中可先准备不依赖 SDK 的核心测试，但不得宣称原生插件已交付。
