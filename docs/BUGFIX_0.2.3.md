# 0.2.3：格内输入与 Illustrator 快捷键隔离

用户实测 0.2.2：双击后出现光标，可以输入，但输入与 AI 快捷键冲突。因此只将“双击进入编辑”记为用户确认，完整输入、拖选和保存仍未验收。

独立 Windows 消息循环回归创建自己的宿主快捷键表，模拟宿主在 DispatchMessage 前先调用 TranslateAccelerator。原实现的 V 被宿主工具快捷键接走，本机和构建 39102701588 均真实失败，失败信息为 `letter typed into editor before host tool accelerator`。单靠输入窗口取得焦点及 WM_GETDLGCODE 无法隔离宿主快捷键。

输入窗口取得焦点时安装当前 UI 线程的 WH_GETMESSAGE 回调，只接收目标为此自定义输入窗口、已从队列移除且焦点仍在此窗口的键盘消息。在交给宿主主循环之前，保留 Windows 字符翻译并向自己的窗口分发一次，再把该条宿主消息改为空消息。其他窗口、其他线程及未移除的 PeekMessage 不接管。失焦、Esc 退出、销毁时解除回调。此设计没有全局键盘监听，不创建任何文字框。

本机 40 项输入/预览断言通过，覆盖字母、Tab、Ctrl+Z、Esc、退出后恢复宿主快捷键、其他焦点窗口、销毁、PM_NOREMOVE 和既有 IME 消息。真实微软拼音/搜狗输入法及 AI 快捷键隔离仍需用户实测；独立消息循环不等于 AI 实机证据。

0.2.2 新日志另显示多次 `input.character.received` 后紧接 `edit.exit.art-changed`。SDK 的 AIArt.h 明确说明 kAIArtObjectsChangedNotifier 在 idle 发送，因此同步 MutationScope 已结束时，插件自己的路径修改通知仍可能到达。需要按当前表格实际发布版本核对，不能不加区分地忽略全部对象变化通知。外部修改、删除、锁定、隐藏和文档切换必须继续使编辑失效。

构建 39102761615 的快捷键回归已转为通过，而新增延迟通知回归按预期失败：`Delayed notification for published artwork must preserve editing`。现在在创建、读取、成功修改表格后记录组及所有子对象的最大编辑时间戳；空闲通知到来时只对仍可编辑且时间戳匹配的当前表格保留会话。输入回调也先核对时间戳，防止在外部修改通知尚未发送时写入旧缓存。外部子对象改动、锁定/隐藏、文档切换及无版本记录均被拒绝。

参考：[Microsoft GetMsgProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/getmsgproc)、[TranslateAccelerator](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-translateacceleratorw)、本机 Adobe Illustrator 2024 SDK AIArt.h 的通知及 GetArtTimeStamp 文档。

最终构建：[39102952373](https://github.com/a769568933/illustrator-table-tool/actions/runs/39102952373)，源码 `5faea601d60e5ab1851b9ef27c4af1038917893c`（本地 `2f40426`）。10 个原生 CTest 全部通过，含快捷键、对象版本、编辑入口回归；7 个模拟用例和原生包检查通过。原生包检查未重复运行的 7 项模拟用例已在前置核心作业运行。独立代码审查未发现 Critical 或 Important 问题；真实 IME 流程仍留待本地验收。

- ZIP SHA-256：`d8ca5e6ff64a104729d2b630165c63787fcb82d6b0e3bfc189490930aa672b67`
- AIP SHA-256：`1397463450a9bb2220dd376ac9c4423f69a5a2e90c1a46fc4242d3e7c79cb170`

确认 AI 已退出后已安装，并校验实际文件版本和摘要。原 0.2.2 备份至项目 `.local/plugin-backups/before-0.2.3-1792303861`。本轮未代为打开 AI、关闭文档或执行实机操作，不将独立消息循环和 SDK 替身测试当作实机通过。
