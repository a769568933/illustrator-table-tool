# 无文字框表格编辑器 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. 沿用 2026-10-16 计划中用户已确认的“在当前对话直接实施”方式；本计划待用户审阅。

**Goal:** 在 Illustrator 28.5 中实现正文底层零文字框的格内编辑，完成模拟与实机验收后交付。

**Architecture:** 表格模型保存 Unicode 文字、格式和合并前数据；DirectWrite 在内存中排版并提取轮廓，AI 只保存路径组。自定义 Windows 输入窗口接收键盘和输入法，宿主事务桥统一提交数据与外观，侧栏继续承载表格操作。

**Tech Stack:** C++17、Illustrator 2024 Windows SDK、MSVC v142、DirectWrite、Direct2D/WIC、IMM32、CMake、GitHub Actions windows-2022、现有 Python 模拟测试。

**Spec:** `docs/superpowers/specs/2026-10-18-frameless-table-editor-design.md`（用户已批准）。

## Global Constraints

- 运行于 Windows x64、Illustrator 2024 28.5。
- 尽量接近 InDesign 的格内编辑方式；合并、拆分、增删行列等操作放在侧边栏。
- **底层也不能使用文字框**。不能使用区域文字、点文字、隐藏文字框或临时文字框来输入、排版、显示、保存格式或转曲。
- 创建插件自己的 Windows 窗口类接收键盘与输入法消息，不使用 EDIT、RichEdit、隐藏文本框或 AI 文字焦点。
- 使用现有公开 GitHub 仓库和云端编译。先模拟测试，再做 Illustrator 实机验收，满足要求后交付。
- 未通过输入法、零文字框、撤销和保存重开这四项前，不交付成品。
- 保留既有上限：1–100 行、1–50 列、总计 2000 格、行高列宽最小 2 毫米；SDK 不进入公开源码或交付包。
- 旧版开发表不自动迁移；旧版文字框实现不进入新版编译目标；PDF 输出说明明确为轮廓文字。

## Review Focus

1. 输入法确认消息伴随 WM_CHAR：结果只能插入一次；Task 5 用消息序列测试，Task 7 实测两种输入法。
2. 合并后只改格式再拆分：原格格式恢复，不能误判为正文修改；Task 1 固定断言。
3. 组字时点侧栏或切文档：提交未完成则不做结构变更，不把候选写到另一文档；Task 5、6 验证会话身份与焦点顺序。
4. 缩放、DPI 或滚动发生在文字拖选期间：光标与命中仍一致，编辑预览不能遮挡侧栏；Task 3、5、7 验证坐标转换和可见区域裁切。
5. 字体失败、复杂字形或生成路径中途报错：原数据及外观不变，无半张表和孤立对象；Task 3、4 注入失败并检查回滚。

## 文件与接口约定

| 文件 | 责任 |
| --- | --- |
| `include/table/RichText.hpp`, `src/core/RichText.cpp` | 格式范围、Unicode 校验、范围替换与合并 |
| `include/table/TextEditor.hpp`, `src/core/TextEditor.cpp` | 格内选区、替换、按簇删除、Tab 导航；不依赖 Windows/SDK |
| `include/table/Types.hpp`, `TableModel.hpp`, `src/core/TableModel.cpp`, `Serialization.cpp` | 格式进入 Cell/OriginalCell、结构往返与 ITT3 持久化 |
| `src/text/TextLayout.hpp`, `DirectWriteLayout.cpp`, `GlyphOutline.cpp` | 排版、命中、字形轮廓、资源预算；不依赖 AI SDK |
| `src/plugin/DocumentAdapter.hpp/.cpp`, `VectorArtwork.hpp/.cpp` | 模型字典、对象识别、矢量组、保护与事务；移除旧文字套件 |
| `src/plugin/InputWindow.hpp/.cpp`, `EditorOverlay.hpp/.cpp` | 自定义窗口、IME、剪贴板、瞬时显示；不修改 AI 文档 |
| `src/plugin/EditController.hpp/.cpp`, `HostCommand.hpp/.cpp`, `Plugin.cpp`, `Sidebar.cpp` | 宿主上下文、输入批次、UI 路由与生命周期 |
| `tests/rich_text_tests.cpp`, `text_editor_tests.cpp`, `text_layout_tests.cpp`, `input_window_tests.cpp` | 各模块可单独运行的回归 |
| `tests/adapter_guard_tests.cpp`, `src/plugin/HostAcceptance.cpp` | SDK 保护回归及真实对象审计 |
| `tests/simulator.cpp`, `simulation/index.html`, `tests/test_simulation.py` | 共用核心的文字与侧栏模拟 |
| `CMakeLists.txt`, `.github/workflows/build.yml`, 打包脚本及 README/验收文档 | 构建、新测试目标、版本 0.2.0 和交付证据 |

统一约定：UTF-16 索引均为 `std::uint32_t`，范围为半开区间；空格文字有效。核心以 `table::ValidationError` 拒绝非法输入，Windows 层在边界把 HRESULT 转为可显示错误。既有 `Cell::text` 保留为唯一正文，不另设重复字符串。

验证命令：便携核心构建使用 `cmake -S . -B build/core -DTABLE_BUILD_PLUGIN=OFF`；Windows SDK 构建使用 `./scripts/build.ps1 -SdkRoot './.local/sdk/Adobe Illustrator 2024 SDK' -HostAcceptance`。下列 `ctest` 均在对应构建编译后运行，Release 配置。没有本地 MSVC 时由现有云端工作流执行 SDK/Windows 测试，不把本机未执行记为通过。

### Task 1：模型格式与新持久格式

**Files:** 新建 RichText.hpp/.cpp、tests/rich_text_tests.cpp；修改 Types.hpp、TableModel.hpp/.cpp、Serialization.cpp、CMakeLists.txt。

**Interfaces:** `TextRun{uint32_t start,length; std::u16string font; double fontSize; Color color;}`；`ParagraphRun{uint32_t start; int alignment;}`；`TextFormatting{std::vector<TextRun> runs; std::vector<ParagraphRun> paragraphs;}`。Cell 和 OriginalCell 增加 `TextFormatting formatting`；空 runs 表示继承 CellStyle。提供 `void validateFormatting(std::u16string_view,const TextFormatting&)`、`void TableModel::setRichText(CellId,const std::u16string&,const TextFormatting&)`。格式编辑不改变正文；拆分是否改字仍比较 mergedText。

- [x] 写 `rich_text_roundtrip`、`merge_split_formats`、`reject_bad_ranges` 测试：ITT3 往返正文/格式相同；24pt 与 12pt 两格合并再拆分恢复；合并后只改样式仍恢复原格；代理对中间边界、越界/重叠 run、重复段落起点被拒绝。16MiB 模型与 1MiB 单段 UTF-8 上限保持；恶意 run 数在分配前拒绝。

  ```cpp
  auto m = TableModel::create(1, 2, mm(40), mm(10));
  m.setRichText(m.cellAt(0,0).id, u"甲", {{{0,1,u"",24,{0,0,0}}},{}});
  const auto before = serialize(m);
  m.merge({0,0,0,1}); m.split(m.cellAt(0,0).id);
  expect(serialize(m) == before, "split restores model formats");
  expect(serialize(deserialize(before)) == before, "ITT3 roundtrip");
  ```
- [x] 配置新增 `table_rich_text_tests` / CTest `table_rich_text`，运行 `ctest --test-dir build/core -C Release -R '^table_rich_text$' --output-on-failure`，先确认失败指向尚未支持的格式保存/验证。
- [x] 实现范围规范化、换行拼接的偏移更新及插删行列时快照传播。ITT3 写格式与快照；核心可读旧 ITT1/ITT2 以继续既有核心测试，但宿主新版只识别新的文档字典，不能据此自动读旧表。样式应用整格时统一更新该格显式格式。
- [x] 运行核心全部测试；检查新建、合并、拆分、插删行列与序列化的既有行为仍通过，格式异常不产生部分修改。
- [x] 仅提交本任务文件：`feat: persist model-owned table text formatting`。

### Task 2：独立文字编辑与切格

**Files:** 新建 TextEditor.hpp/.cpp、tests/text_editor_tests.cpp；修改 CMakeLists.txt。

**Interfaces:** `EditSession{CellId cell; uint32_t anchor,caret; uint64_t batch;}`；`EditChange{TableModel model; EditSession session; bool changed; bool addedRow;}`；`replaceSelection(const TableModel&,const EditSession&,std::u16string_view)->EditChange`；`eraseCluster(const TableModel&,const EditSession&,const std::vector<uint32_t>& boundaries,bool backward)->EditChange`；`navigateCell(const TableModel&,const EditSession&,bool backward)->EditChange`。簇边界由 Task 3 排版结果提供，包含 0 和 text.size()。

- [x] 写 `replace_selected_chinese`、`delete_whole_cluster`、`tab_visits_merge_once`、`last_tab_respects_limits`：选择“中文”替换为“表格”；`A😀é` 后退分别删除整个 é 和 😀；2×2 合并格只访问一次；100 行末格 Tab 报错且原模型/位置不变，第一格 Shift+Tab 不变。换行统一为 LF；嵌入 NUL 和无配对代理项拒绝。

  ```cpp
  auto m = TableModel::create(1,1,mm(20),mm(10));
  const auto id = m.cellAt(0,0).id; m.setText(id,u"A\U0001F600e\u0301");
  auto changed = eraseCluster(m,{id,5,5,0},{0,1,3,5},true);
  expect(changed.model.cellById(id).text == u"A\U0001F600", "delete full cluster");
  expect(changed.session.caret == 3, "caret stays on cluster boundary");
  ```
- [x] 注册 CTest `table_text_editor`，运行 `ctest --test-dir build/core -C Release -R '^table_text_editor$' --output-on-failure` 确认上述未实现行为失败。
- [x] 实现纯函数编辑、格式范围随替换调整、按可见锚格行序导航。末格新增行使用最后一行高度并检查全部既有上限；异常不修改传入模型/会话。输入新文字继承插入位置格式，空格继承 CellStyle。
- [x] 运行 `table_text_editor` 与核心回归；增加选区反向、空正文、首尾删除和连续撤销所需前后模型快照的确定性检查。
- [x] 提交：`feat: add model-owned cell text editing and navigation`。

### Task 3：Windows 排版、命中及直接轮廓

**Files:** 新建 src/text/TextLayout.hpp、DirectWriteLayout.cpp、GlyphOutline.cpp、tests/text_layout_tests.cpp；修改 CMakeLists.txt、.github/workflows/build.yml。

**Interfaces:** `LayoutRequest{std::u16string text; TextFormatting formatting; CellStyle style; double widthPt,heightPt;}`；`TextLayout` 提供 `hitTest(Point)->uint32_t`、`caret(uint32_t)->Rect`、`selection(uint32_t,uint32_t)->std::vector<Rect>`、`boundaries()->std::vector<uint32_t>`、`outlines()->const std::vector<GlyphPath>&`、`overflows()->bool`。`GlyphPath` 含轮廓（起点、直线/三次曲线、封闭标志）、颜色及填充规则。`layoutText(const LayoutRequest&)->std::shared_ptr<const TextLayout>`。

- [x] 写 `chinese_layout_clusters`、`outline_holes_and_curves`、`pt_dpi_roundtrip`、`layout_failure_has_no_output`：中英文/换行有正确 caret/selection，é 不可拆；“O8中”有轮廓孔洞且有曲线；12pt=16 DIP，25%/100%/400% 映射反算误差小于 0.1pt；缺失字体产生明确替代信息；不可用轮廓及预算不足直接失败。

  ```cpp
  auto layout = layoutText({u"e\u0301",{},CellStyle{},100,40});
  expect(layout->boundaries() == std::vector<uint32_t>({0,2}), "combining cluster");
  expect(!layout->outlines().empty(), "direct glyph contours");
  expect(!layout->overflows(), "short text fits");
  ```
- [x] 建立不依赖 Adobe SDK 的 `table_text_layout_tests` / CTest `table_text_layout`，Windows CI 在核心作业内编译并运行，确认测试先失败。
- [x] 用 DirectWrite layout + 自定义 IDWriteTextRenderer + GetGlyphRunOutline 实现；布局返回确定的 font fallback 及 overflow 信息。混合格式范围用 DirectWrite 范围接口；不按字符逐字绘制，保留字形偏移和连字。枚举可用 Windows 字体供侧栏参数使用，不获取 ATE 字体。
- [x] 测试并记录 4×4、20×20 表和 2000 个空格的排版耗时、内存及输出规模；先设单格 32768 UTF-16 单元、32768 字形、250000 曲线段、整表 1000000 段的保守运行预算。该限制只影响新提交，超限拒绝且说明原因；根据记录只可收紧，放宽须重新测试。运行 Windows CTest 全部通过。
- [x] 提交：`feat: shape text and generate glyph contours without text frames`。

### Task 4：替换 AI 对象适配器并建立零文字框审计

**Files:** 修改 DocumentAdapter.hpp/.cpp、tests/adapter_guard_tests.cpp、HostAcceptance.cpp、CMakeLists.txt；新建 VectorArtwork.hpp/.cpp、tests/test_frameless_source.py。

**Interfaces:** `NativeCell{id,fill,content}` 的 content 为分组句柄；移除 text/archive/snapshots。保留 createTable、valid、discoverTables、synchronize、applyGeometry、applyStructure、applyStyle、detach 的外部用途；新增 `applyText(NativeTable&,const TableModel&)->void`、`auditVectorTable(Suites&,AIArtHandle)->void`。删除 beginEdit，溢出由 TextLayout 得出。文档键为 `IllustratorTableTool.Model.v3`，角色键与所有权仍严格验证。

- [ ] 写源代码约束测试，禁止产品编译源中 AITextFrame、ATESuites、SetTextFocus、kTypeTool 及文字转曲调用；先在旧实现上确认失败。更新宿主测试：创建“中文 O8”并递归审计全部子对象，`TextFrameArt==0` 且全部类型在许可集合中；只删除 UI 提示不算通过。

  ```python
  # test_frameless_source: product_sources comes from the shipping target's source list.
  for path in product_sources:
      source = path.read_text(encoding="utf-8")
      for forbidden in ("AITextFrame", "ATESuites", "SetTextFocus", "kTypeTool"):
          self.assertNotIn(forbidden, source, str(path))
  ```
- [ ] 运行源约束与 `adapter_guards`，确认失败来自旧适配器或尚无矢量内容；开发验收只在新建测试文档运行。
- [ ] 删除旧原生文字和隐藏快照实现，以 Task 3 轮廓建普通/复合路径并裁切于格内。VectorArtwork 负责坐标/曲线/孔洞/颜色，DocumentAdapter 负责字典/关联/保护/事务；新模型的原格数据是唯一拆分来源。先准备内存内容，再创建替代组，成功后替换；捕获失败调用当前上下文 UndoChanges，重新识别，不能留下新缓存。
- [ ] 保留并运行过期句柄、关闭文档、锁定隐藏、损坏表隔离测试；注入第 N 条路径创建失败，确认模型字典、原路径和兄弟表均不变，无孤立对象。宿主测试对新建、文字修改、合并拆分、复制、保存重开逐次断言零文字框。
- [ ] 提交：`refactor: render table contents only as vector artwork`。

### Task 5：自定义输入、IME 和瞬时画布预览

**Files:** 新建 InputWindow.hpp/.cpp、EditorOverlay.hpp/.cpp、tests/input_window_tests.cpp；修改 CMakeLists.txt。

**Interfaces:** `InputKind` 枚举为 `Insert, Key, Composition, Commit, Cancel, FocusLost`；`InputEvent{InputKind kind; std::u16string text; uint32_t key,modifiers,compositionStart,compositionLength;}`。`CompositionUpdate{std::u16string preedit,result; uint32_t caret; bool resultAvailable;}`；`CompositionReader` 为 `std::function<CompositionUpdate(HWND,LPARAM)>`，生产默认读取 IMM32，测试可注入已解码结果。`InputWindow::create(HWND owner,std::function<void(const InputEvent&)>,CompositionReader reader={})`、`focus()`、`requestCompositionCommit()->bool`、`cancelComposition()`、`destroy()`。`EditorOverlay::show(const OverlayFrame&)` / `hide()`；OverlayFrame 含屏幕裁切区域、当前 cell layout、caret/selection 和 DPI，坐标已由宿主控制器换算。

- [ ] 写 `custom_window_has_no_edit_children`、`ime_commit_once`、`ime_cancel_keeps_model`、`clipboard_unlock_on_failure`：窗口类名固定 `IllustratorTableTool.Input.v3`，子树无 EDIT/RichEdit；组字结果随后 WM_CHAR 不重复；Esc 只取消组字；剪贴板 Unicode 数据有界且关闭释放。禁止用标准编辑控件作隐藏代理。

  ```cpp
  InputWindow input; input.create(nullptr, [](const InputEvent&){});
  wchar_t name[128]{}; GetClassNameW(input.handle(),name,128);
  expect(std::wstring(name) == L"IllustratorTableTool.Input.v3", "custom input class");
  input.destroy(); expect(input.handle() == nullptr, "window released");
  ```

  为该测试提供只读 `InputWindow::handle()->HWND`。IME 序列测试通过窗口层的消息解析接口注入已解码的 composition/result，另用真实 IMM32/输入法实测验证读取路径；不能用伪造消息绕过真实输入法关卡。
- [ ] 注册 Windows CTest `table_input_window`，运行并确认目标行为先失败；消息测试不能代替真实输入法验收。
- [ ] 自定义 WndProc 处理 WM_CHAR、WM_KEYDOWN、WM_IME_* 与剪贴板；正确处理 ImmGetCompositionStringW 的字节长度和代理项缓存。只发送 InputEvent，不访问 AI SDK。候选窗锚定 caret；结构操作请求提交必须等待结果完成，不能凭调用成功就清空组字。
- [ ] 瞬时显示使用自定义 owned layered 窗口，Direct2D/WIC 绘制活动格背景、完整文字、组字、光标和选区，不写文档 artwork；复用 Task 3 布局。裁切至格和文档可见区域，避开侧栏；失焦和应用失活隐藏。测试 96/144/192 DPI 及滚动更新，关闭窗口后无定时器、回调或 COM 资源残留。
- [ ] 运行 Windows 测试并提交：`feat: receive cell input and IME through custom windows`。

### Task 6：接通格内交互、侧栏和宿主撤销

**Files:** 新建 EditController.hpp/.cpp、HostCommand.hpp/.cpp；修改 Plugin.cpp、Sidebar.cpp、DocumentAdapter.cpp、tests/input_window_tests.cpp、HostAcceptance.cpp。

**Interfaces:** `EditController::begin(NativeTable&,CellId,Point)`、`handle(const InputEvent&)`、`beforeStructure()->bool`、`end(bool commitComposition)`、`refreshView()`。HostCommand 提供 `invokeHostCommand(SPBasicSuite*,SPPluginRef,const std::function<void(Suites&)>&)->void`，RAII 包装 PushAppContext/PopAppContext；当前应用 owner 取 SDK GetPlatformAppWindow。会话身份包含 document、table group、cell ID 和 generation。

- [ ] 写 `stale_input_session_rejected`、`pending_ime_blocks_structure`、`failed_apply_keeps_session`、`typing_batch_breaks_on_navigation`。宿主新增断言：一次输入法确认/粘贴/合并/拖动分别一次撤销恢复原模型和路径；重做恢复新内容。连续普通输入同批次撤销，切格后不得并入旧批次。

  ```cpp
  // Inside the real host acceptance callback, before/after are serialized model snapshots.
  checked(undo->MultiUndoTransaction(document,1));
  adapter.synchronize(native);
  expect(serialize(native.model) == before, "one host undo restores text and model");
  checked(undo->MultiRedoTransaction(document,1));
  adapter.synchronize(native);
  expect(serialize(native.model) == after, "redo restores committed edit");
  auditVectorTable(suites,native.group);
  ```
- [ ] 运行模块测试与开发宿主验收，先确认缺少路由/事务导致失败。验证原生 SDK 的应用上下文就是操作边界；不在通知或绘制回调中修改文档。
- [ ] 双击改为自定义 begin，不切到 AI 文字工具；编辑中点击/拖动经 layout 命中，Shift 单击保持格区域选择；Tab/Shift+Tab/Enter/Esc 按方案处理。侧栏每条结构/样式命令先经 beforeStructure，再走同一核心与适配器。成功提交后才接受新 session；不在辅助绘制回调里同步正文。
- [ ] 宿主桥在自己的非嵌套上下文中为新操作设 standard undo，连续普通输入才使用 append；遇到嵌套、非本插件事务或批次失效时开始独立操作，不强行并入其他工具的撤销。文档切换/关闭、工具切换、undo/redo、锁定隐藏和移动缩放时重新验证或结束会话。清除回调并释放插件 pin 后才能卸载。
- [ ] 运行全部 Windows/SDK 回归及实际宿主事务测试，提交：`feat: integrate frameless editing with sidebar and host history`。

### Task 7：模拟、实机验收和成品打包

**Files:** 修改 tests/simulator.cpp、scripts/simulation_server.py、simulation/index.html、tests/test_simulation.py、HostAcceptance.cpp、build.yml、CMakeLists.txt、README.md、docs/HOST_ACCEPTANCE.md、打包脚本。

**Interfaces:** 模拟器新增 edit/selectText/deleteCluster/tab 动作，JSON 返回 cells 中的正文/格式，以及 editor 对象 `{cellId,anchor,caret,batch}`；仍调用 Task 1/2 的 C++ 核心。编辑请求包含现有 state 以及 row/column/anchor/caret；edit 附 text，deleteCluster 附 boundaries/backward，tab 附 backward。新动作通过有界的长度前缀 stdin 块传递模型与正文，正文不放命令行；旧动作协议保留。浏览器模拟的簇来自 Intl.Segmenter，固定组合字符测试传明确 boundaries；其结果不计为 DirectWrite 排版通过。自动宿主入口保留仅显式标记且无用户文档才新建测试文档的保护，新增阶段名 `frameless.*`，失败日志不记录用户文字。

- [ ] 先写模拟流程断言：输入“中文\n123”、拖选替换、连续删除组合字符、跨合并格 Tab、末格增行、合并/拆分格式、序列化重开、错误输入保留原状态；让当前未接通模拟动作失败。模拟画布用 canvas 与自定义键盘/组字事件，不用 DOM 正文输入框；明确标为模拟，不声称 DirectWrite/AI 结果。

  ```python
  executable = Path(os.environ["TABLE_SIMULATOR"])
  state = server.simulate(executable, {"action":"create", "rows":1, "columns":1})
  state = server.simulate(executable, {"action":"edit", "state":state["state"],
      "row":0, "column":0, "anchor":0, "caret":0, "text":"中文\n123"})
  self.assertEqual(state["cells"][0]["text"], "中文\n123")
  state = server.simulate(executable, {"action":"edit", "state":state["state"],
      "row":0, "column":0, "anchor":0, "caret":2, "text":"表格"})
  self.assertEqual(state["cells"][0]["text"], "表格\n123")
  self.assertEqual(state["editor"]["caret"], 2)
  ```
- [ ] 实现模拟动作并运行 `python -m unittest discover -s tests -p 'test_*.py' -v`，保留既有 480 次结构往返，新增编辑流程；运行云端核心、Windows、SDK、零文字框源约束、PE 和包校验，记录实际 run、SHA 和日志。
- [ ] 安装前读取最新应用状态，AI 正在运行时不替换已加载文件。仅在新建测试文档验收：搜狗/微软拼音候选确认取消、拖选、剪贴板、Tab、末格增行、侧栏合并拆分增删、停靠、25%/100%/400%、撤销重做、整表移动缩放、RGB/CMYK、锁定隐藏/坏表保护。对无法由桌面工具证明的项目明确待验证，请用户只协助该具体操作，不记为通过。
- [ ] 保存 AI、关闭重开再改字/拆分/复制，审计隐藏对象仍零文字框；导出 PDF 并检查轮廓孔洞、裁切、印刷外观和无辅助显示。文档落点使用当前画板可见范围，避免旧验收表落到屏幕外。模拟通过或自动 SDK 通过都不能替代 IME/停靠/导出目测。
- [ ] 全部关卡通过后更新版本为 0.2.0，README 写明输入方式、兼容边界和 PDF 轮廓限制；正式包关闭 TABLE_HOST_ACCEPTANCE 并复验加载。提交 `feat: validate and package frameless table editor`，交付 AIP ZIP、SHA-256、构建链接与实际验收记录；未通过的功能不得宣称完成。

## 自审与执行交接

- 需求覆盖：零文字框 Task 4/7；正文数据/格式 Task 1；文字编辑/Tab Task 2/6；轮廓/溢出 Task 3/4；输入法/候选/取消 Task 5/6/7；侧栏/事务/生命周期 Task 6；模拟与交付 Task 7。
- 类型检查：正文只在 Cell.text；格式只在 Cell.formatting；预编辑只在输入会话；DirectWrite 为唯一 Windows 排版与簇边界来源；SDK 仅由 HostCommand/插件回调访问。
- 五项 Review Focus 已分别有指定测试，不以网页模拟替代 SDK 或 IME 验收。
- 执行方式沿用当前会话直接实施；先请用户审阅本计划。不得把“设计确认”记为“计划已审阅”，不得在计划待审时安装依赖或开始产品代码修改。
