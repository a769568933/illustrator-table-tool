# Illustrator Table Tool / AI 画布表格工具

为 Adobe Illustrator 2024 (28.5) Windows x64 开发的原生画布表格工具。

**状态：开发中。原生 x64 加载探针已通过云端编译，并在 Illustrator 28.5 中成功加载和执行中文菜单命令。当前交互版正在验证，尚未交付完整表格插件。**

目标是在画布中选取单元格、拖动行列边线，并使用 Illustrator 原生文字引擎输入中文。源代码不包含 Adobe SDK；开发者须从 Adobe Developer Console 获取对应版本的 Windows C++ SDK。

开发设计和实施计划位于 `docs/superpowers/`。所有实际验证结果将记录于 `docs/HOST_ACCEPTANCE.md`。

无 SDK 的核心构建：`cmake -S . -B build/core -DTABLE_BUILD_PLUGIN=OFF`，编译后运行 `ctest --test-dir build/core -C Release --output-on-failure`。

开发版入口位于“对象”菜单。新建 2×2 测试表格后在画布单击放置；再次使用“选择和调整单元格”进入表格工具。交互版的双击文字、拖线和撤销尚待实机验收；保存重开和合并拆分尚未实现，请使用测试文档。
