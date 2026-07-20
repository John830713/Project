// Demonstrates DebugGetState() pattern: a text query interface
// for inspecting module/manager state without visual verification.
//
// Compiles standalone with just the project headers — no module .cpp files.
//
// mingw32-make Debug/Core/DebugStateTest.exe
// (or add to test: target in Makefile)

#include <cstdio>
#include <cassert>
#include <string>
#include <vector>

//--- Project headers (header-only, no link deps) ---
#include "Core/IFeatureModule.h"
#include "Core/ModuleManager.h"
#include "Core/IDebugStateProvider.h"

//--- Test helpers ---
static int g_tests = 0;
static int g_fails = 0;

#define CHECK(cond, msg) do { \
    g_tests++; \
    if (!(cond)) { \
        printf("FAIL %s\n", msg); \
        g_fails++; \
    } else { \
        printf("OK   %s\n", msg); \
    } \
} while(0)

#define CHECK_CONTAINS(text, substr, msg) do { \
    g_tests++; \
    if ((text).find(substr) == std::wstring::npos) { \
        printf("FAIL %s\n  expected '%ls' in:\n  %ls\n", msg, substr, (text).c_str()); \
        g_fails++; \
    } else { \
        printf("OK   %s\n", msg); \
    } \
} while(0)

//--- A minimal test module that implements IFeatureModule only ---
class BareModule : public IFeatureModule {
public:
    explicit BareModule(const wchar_t* name) : m_name(name) {}
    const wchar_t* GetModuleName() const override { return m_name; }
    const wchar_t* GetDisplayName() const override { return m_name; }
    const wchar_t* GetConfigFileName() const override { return L""; }
    const wchar_t* GetConfigSectionName() const override { return L""; }
    const wchar_t* GetLogSourceName() const override { return L""; }
    const std::vector<ConfigFieldDefinition>& GetConfigDefinitions() const override {
        static std::vector<ConfigFieldDefinition> empty; return empty;
    }
    void Initialize(HINSTANCE, IHostContext*) override {}
    void LoadConfig() override {}
    void SaveConfig() override {}
    void ApplyConfig() override {}
    void Shutdown() override {}
    std::wstring GetValue(const std::wstring&) const override { return {}; }
    void SetValue(const std::wstring&, const std::wstring&) override {}
private:
    const wchar_t* m_name;
};

//--- A minimal test module that implements both interfaces ---
class TestModule : public IFeatureModule, public IDebugStateProvider {
public:
    explicit TestModule(const wchar_t* name) : m_name(name) {}
    const wchar_t* GetModuleName() const override { return m_name; }
    const wchar_t* GetDisplayName() const override { return m_name; }
    const wchar_t* GetConfigFileName() const override { return L""; }
    const wchar_t* GetConfigSectionName() const override { return L""; }
    const wchar_t* GetLogSourceName() const override { return L""; }
    const std::vector<ConfigFieldDefinition>& GetConfigDefinitions() const override {
        static std::vector<ConfigFieldDefinition> empty; return empty;
    }
    void Initialize(HINSTANCE, IHostContext*) override {}
    void LoadConfig() override {}
    void SaveConfig() override {}
    void ApplyConfig() override {}
    void Shutdown() override {}
    std::wstring GetValue(const std::wstring&) const override { return {}; }
    void SetValue(const std::wstring&, const std::wstring&) override {}

    void SetDebugState(const std::wstring& s) { m_debugState = s; }
    std::wstring DebugGetState() const override { return m_debugState; }

private:
    const wchar_t* m_name;
    std::wstring m_debugState;
};

//--- Test: ModuleManager bare ---
void TestManagerEmpty() {
    printf("\n=== ModuleManager empty ===\n");
    ModuleManager mgr;
    auto s = mgr.DebugGetState();
    CHECK_CONTAINS(s, L"Modules: 0", "no modules");
}

//--- Test: ModuleManager with one module ---
void TestManagerOneModule() {
    printf("\n=== ModuleManager + 1 module ===\n");
    ModuleManager mgr;
    auto* mod = new TestModule(L"DemoMod");
    mod->SetDebugState(L"  MyState: active\n");
    mgr.RegisterModule(mod);
    auto s = mgr.DebugGetState();
    CHECK_CONTAINS(s, L"Modules: 1", "1 module");
    CHECK_CONTAINS(s, L"DemoMod", "module name visible");
    CHECK_CONTAINS(s, L"MyState: active", "module state included");
    // ModuleManager destructor will delete mod
}

//--- Test: ModuleManager with module that has NO debug state ---
void TestManagerNoState() {
    printf("\n=== ModuleManager + module w/o IDebugStateProvider ===\n");
    ModuleManager mgr;
    auto* mod = new BareModule(L"SilentMod");
    mgr.RegisterModule(mod);
    auto s = mgr.DebugGetState();
    CHECK_CONTAINS(s, L"Modules: 1", "1 module");
    CHECK_CONTAINS(s, L"(no debug state)", "fallback text present");
}

//--- Test: module runtime flags independently queried ---
void TestModuleDirectQuery() {
    printf("\n=== Direct module query ===\n");
    TestModule mod(L"FlagDemo");
    mod.SetDebugState(L"  FlagA: 1\n  FlagB: 0\n");
    auto s = mod.DebugGetState();
    CHECK(!s.empty(), "state is non-empty");
    CHECK_CONTAINS(s, L"FlagA: 1", "FlagA visible");
    CHECK_CONTAINS(s, L"FlagB: 0", "FlagB visible");
}

//--- Main ---
int main() {
    TestManagerEmpty();
    TestManagerOneModule();
    TestManagerNoState();
    TestModuleDirectQuery();

    printf("\n=== Results: %d tests, %d failures ===\n", g_tests, g_fails);
    return g_fails > 0 ? 1 : 0;
}
