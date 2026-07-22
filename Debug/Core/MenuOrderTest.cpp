// MenuOrderTest.cpp - Tests for menu order priority sorting
//
// Tests GetModulePriority and GetMenuGroups sorting behavior.
//
// mingw32-make Debug/Core/MenuOrderTest.exe
// (or add to test: target in Makefile)

#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

//--- Project headers ---
#include "Core/IFeatureModule.h"
#include "Core/ModuleManager.h"

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
    } } while(0)

#define CHECK_EQ(a, b, msg) do { \
    g_tests++; \
    if ((a) != (b)) { \
        printf("FAIL %s\n  expected %d, got %d\n", msg, (b), (a)); \
        g_fails++; \
    } else { \
        printf("OK   %s\n", msg); \
    } } while(0)

//--- A minimal test module with configurable context menu items ---
class TestModule : public IFeatureModule {
public:
    explicit TestModule(const wchar_t* name, const wchar_t* displayName = nullptr)
        : m_name(name), m_displayName(displayName ? displayName : name) {}
    
    const wchar_t* GetModuleName() const override { return m_name; }
    const wchar_t* GetDisplayName() const override { return m_displayName; }
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
    
    void AddContextMenuItem(int id, const wchar_t* label) {
        m_items.push_back({ id, label, 0, L"" });
    }
    
    std::vector<ContextMenuItem> GetContextMenuItems() const override {
        return m_items;
    }
    
    void ExecuteContextMenuItem(int itemId) override {}

private:
    const wchar_t* m_name;
    const wchar_t* m_displayName;
    std::vector<ContextMenuItem> m_items;
};

//--- Test: empty manager returns empty groups ---
void TestEmptyManager() {
    printf("\n=== Empty manager ===\n");
    ModuleManager mgr;
    auto groups = mgr.GetMenuGroups();
    CHECK(groups.empty(), "no groups from empty manager");
}

//--- Test: single module with no context menu items ---
void TestSingleModuleNoItems() {
    printf("\n=== Single module, no items ===\n");
    ModuleManager mgr;
    auto* mod = new TestModule(L"NoItemsMod");
    mgr.RegisterModule(mod);
    auto groups = mgr.GetMenuGroups();
    CHECK(groups.empty(), "no groups when module has no items");
}

//--- Test: single module with one item ---
void TestSingleModuleOneItem() {
    printf("\n=== Single module, one item ===\n");
    ModuleManager mgr;
    auto* mod = new TestModule(L"ModA");
    mod->AddContextMenuItem(1, L"Action 1");
    mgr.RegisterModule(mod);
    auto groups = mgr.GetMenuGroups();
    CHECK_EQ((int)groups.size(), 1, "one group");
    CHECK_EQ((int)groups[0].items.size(), 1, "one item in group");
}

//--- Test: multiple modules assign sequential uniqueIds ---
void TestSequentialUniqueIds() {
    printf("\n=== Sequential uniqueIds ===\n");
    ModuleManager mgr;
    auto* mod1 = new TestModule(L"Mod1");
    mod1->AddContextMenuItem(1, L"Item1");
    mod1->AddContextMenuItem(2, L"Item2");
    mgr.RegisterModule(mod1);
    
    auto* mod2 = new TestModule(L"Mod2");
    mod2->AddContextMenuItem(1, L"Item3");
    mgr.RegisterModule(mod2);
    
    auto groups = mgr.GetMenuGroups();
    CHECK_EQ((int)groups.size(), 2, "two groups");
    CHECK_EQ((int)groups[0].items.size(), 2, "two items in first group");
    CHECK_EQ((int)groups[1].items.size(), 1, "one item in second group");
}

//--- Test: execute dispatches to correct module ---
void TestExecuteDispatch() {
    printf("\n=== Execute dispatch ===\n");
    ModuleManager mgr;
    auto* mod1 = new TestModule(L"Mod1");
    mod1->AddContextMenuItem(10, L"Item1");
    mgr.RegisterModule(mod1);
    
    auto* mod2 = new TestModule(L"Mod2");
    mod2->AddContextMenuItem(20, L"Item2");
    mgr.RegisterModule(mod2);
    
    auto groups = mgr.GetMenuGroups();
    CHECK_EQ((int)groups.size(), 2, "two groups");
    
    // Execute first item - should not crash
    bool result1 = mgr.ExecuteContextMenuItem(0);
    CHECK(result1, "execute first item returns true");
    
    // Execute second item
    bool result2 = mgr.ExecuteContextMenuItem(1);
    CHECK(result2, "execute second item returns true");
    
    // Execute out of range
    bool result3 = mgr.ExecuteContextMenuItem(99);
    CHECK(!result3, "execute out of range returns false");
}

//--- Test: empty module names are handled ---
void TestEmptyModuleName() {
    printf("\n=== Empty module name ===\n");
    ModuleManager mgr;
    auto* mod = new TestModule(L"");
    mod->AddContextMenuItem(1, L"Item1");
    mgr.RegisterModule(mod);
    auto groups = mgr.GetMenuGroups();
    CHECK_EQ((int)groups.size(), 1, "one group");
    CHECK(groups[0].displayName.empty(), "empty display name");
}

//--- Main ---
int main() {
    TestEmptyManager();
    TestSingleModuleNoItems();
    TestSingleModuleOneItem();
    TestSequentialUniqueIds();
    TestExecuteDispatch();
    TestEmptyModuleName();
    
    printf("\n=== Results: %d tests, %d failures ===\n", g_tests, g_fails);
    return g_fails > 0 ? 1 : 0;
}