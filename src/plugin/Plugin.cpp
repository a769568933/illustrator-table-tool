#include <windows.h>
#include "SPBasic.h"
#include "SPBlocks.h"
#include "SPInterf.h"
#include "AIPlugin.h"
#include "AIMenu.h"
#include "AIMenuGroups.h"
#include "AIUnicodeString.h"
#include "IAIUnicodeString.h"
#include <cstring>
#include <exception>
#include <vector>

extern "C" {
AIUnicodeStringSuite* sAIUnicodeString = nullptr;
SPBlocksSuite* sSPBlocks = nullptr;
}

namespace {
struct State { AIMenuItemHandle about = nullptr; };

class Suites {
    SPBasicSuite* basic;
    struct Acquired { const char* name; ai::int32 version; };
    std::vector<Acquired> acquired;
public:
    explicit Suites(SPBasicSuite* value) : basic(value) {}
    ~Suites() {
        for (auto it = acquired.rbegin(); it != acquired.rend(); ++it)
            basic->ReleaseSuite(it->name, it->version);
        sAIUnicodeString = nullptr;
        sSPBlocks = nullptr;
    }
    template<class T> T* get(const char* name, ai::int32 version) {
        const void* result = nullptr;
        const auto error = basic->AcquireSuite(name, version, &result);
        if (error || !result) throw error ? error : kCantHappenErr;
        acquired.push_back({name, version});
        return const_cast<T*>(static_cast<const T*>(result));
    }
};
}

// Adobe's SPMessageData owns the persistent globals pointer. No cached suite
// pointers survive an unload; leases are acquired afresh for each callback.
extern "C" ASAPI ASErr PluginMain(char* caller, char* selector, void* message) {
    if (!caller || !selector || !message) return kBadParameterErr;
    auto* data = static_cast<SPMessageData*>(message);
    if (!data->basic) return kBadParameterErr;
    try {
        Suites suites(data->basic);
        sSPBlocks = suites.get<SPBlocksSuite>(kSPBlocksSuite, kSPBlocksSuiteVersion);
        sAIUnicodeString = suites.get<AIUnicodeStringSuite>(kAIUnicodeStringSuite, kAIUnicodeStringVersion);
        if (std::strcmp(caller, kSPInterfaceCaller) == 0) {
            if (std::strcmp(selector, kSPInterfaceStartupSelector) == 0) {
                auto* state = new State;
                data->globals = state;
                try {
                    auto* menus = suites.get<AIMenuSuite>(kAIMenuSuite, kAIMenuVersion);
                    AIPlatformAddMenuItemDataUS item;
                    item.groupName = kObjectUtilsMenuGroup;
                    item.itemText = ai::UnicodeString::FromUTF8("表格工具：关于与加载测试…");
                    const auto error = menus->AddMenuItem(data->self, "IllustratorTableTool.About", &item, 0, &state->about);
                    if (error) throw error;
                } catch (...) { delete state; data->globals = nullptr; throw; }
                return kNoErr;
            }
            if (std::strcmp(selector, kSPInterfaceShutdownSelector) == 0) {
                delete static_cast<State*>(data->globals);
                data->globals = nullptr;
                return kNoErr;
            }
        }
        if (std::strcmp(caller, kCallerAIMenu) == 0 && std::strcmp(selector, kSelectorAIGoMenuItem) == 0) {
            auto* state = static_cast<State*>(data->globals);
            auto* menuMessage = static_cast<AIMenuMessage*>(message);
            if (state && menuMessage->menuItem == state->about)
                MessageBoxW(nullptr, L"Illustrator Table Tool\n原生插件入口已加载。\n此为开发阶段的加载测试，尚无表格编辑功能。", L"AI 画布表格工具", MB_OK | MB_ICONINFORMATION);
        }
        return kNoErr;
    } catch (ASErr error) { return error; }
    catch (const std::exception&) { return kCantHappenErr; }
    catch (...) { return kCantHappenErr; }
}
