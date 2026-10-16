# SDK 与编译环境

本项目针对 Illustrator 28.5 Windows x64。已检查用户从 Adobe 官方下载的 `AI_2024_SDK_Win.zip`：

- 大小：30,622,241 字节。
- SHA-256：`d6f3c8c86e211e4ceedd3434d0d82d7d9b394fa0eb06c402aeb5eae90a48e65c`。
- 解压根目录：`Adobe Illustrator 2024 SDK`。
- 官方 Windows 样例：C++17，v142 工具集，`/MD`，8 字节结构体对齐，`/Zc:wchar_t-`，`/Zc:sizedDealloc-`。
- 官方 PiPL 生成器：`tools/pipl/create_pipl.py`，样例将生成的 `plugin.pipl` 作为 `plugin pipl` 类型资源嵌入 Windows DLL，并导出 `PluginMain`。

本地 SDK 位于被 Git 忽略的 `.local/sdk/`。SDK 压缩包、头文件、库和示例不会提交到公开源码树。

原生编译必须使用真实 SDK；缺失时明确报错，不能生成占位插件。云端 SDK 的私有传递方式尚待配置。
