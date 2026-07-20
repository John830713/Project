// Unit tests for AutoKey parser and INI roundtrip
#include "Modules/AutoKey/AutoKeyParser.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

//==============================================================================
// Minimal test framework
//==============================================================================
static int g_tests = 0;
static int g_fails = 0;

#define ASSERT_EQ(a, b) do { \
    g_tests++; \
    if ((a) != (b)) { \
        printf("FAIL [%s:%d] expected '%s' == '%s'\n", __FILE__, __LINE__, #a, #b); \
        g_fails++; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    g_tests++; \
    if (!(cond)) { \
        printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond); \
        g_fails++; \
    } \
} while(0)

static void ASSERT_CHORD(const ParsedChord& c, WORD expectedKey,
                          const std::vector<WORD>& expectedMods) {
    g_tests++;
    if (c.key != expectedKey) {
        printf("FAIL [%s:%d] chord.key: expected 0x%04X, got 0x%04X\n",
               __FILE__, __LINE__, expectedKey, c.key);
        g_fails++;
    }
    if (c.modifiers.size() != expectedMods.size()) {
        printf("FAIL [%s:%d] modifiers.size: expected %zu, got %zu\n",
               __FILE__, __LINE__, expectedMods.size(), c.modifiers.size());
        g_fails++;
    } else {
        for (size_t i = 0; i < c.modifiers.size(); ++i) {
            if (c.modifiers[i] != expectedMods[i]) {
                printf("FAIL [%s:%d] modifier[%zu]: expected 0x%04X, got 0x%04X\n",
                       __FILE__, __LINE__, i, expectedMods[i], c.modifiers[i]);
                g_fails++;
            }
        }
    }
}

//==============================================================================
// Test: KeyNameToVk
//==============================================================================
static void test_KeyNameToVk() {
    // F-keys
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"F1"),  VK_F1);
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"F13"), 0x7C);   // VK_F13
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"F24"), 0x87);   // VK_F24

    // Alpha keys
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"A"), 'A');
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"Z"), 'Z');

    // Named keys
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"SCROLL_LOCK"), VK_SCROLL);
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"SCROLLLOCK"),  VK_SCROLL);
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"CAPS_LOCK"),   VK_CAPITAL);
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"CAPSLOCK"),    VK_CAPITAL);
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"NUM_LOCK"),    VK_NUMLOCK);
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"NUMLOCK"),     VK_NUMLOCK);
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"SHIFT"), VK_SHIFT);
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"CTRL"),  VK_CONTROL);
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"ALT"),   VK_MENU);
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L"WIN"),   VK_LWIN);

    // Empty → default F13
    ASSERT_EQ(AutoKeyParser::KeyNameToVk(L""), 0x7C);

    printf("  test_KeyNameToVk: %d/%d passed\n", g_tests - g_fails, g_tests);
}

//==============================================================================
// Test: ModNameToVk / ModNameToRegFlag
//==============================================================================
static void test_ModNameToVk() {
    ASSERT_EQ(AutoKeyParser::ModNameToVk(L"Ctrl"),  VK_CONTROL);
    ASSERT_EQ(AutoKeyParser::ModNameToVk(L"Shift"), VK_SHIFT);
    ASSERT_EQ(AutoKeyParser::ModNameToVk(L"Alt"),   VK_MENU);
    ASSERT_EQ(AutoKeyParser::ModNameToVk(L"Win"),   VK_LWIN);
    ASSERT_EQ(AutoKeyParser::ModNameToVk(L"CTRL"),  VK_CONTROL);
    ASSERT_EQ(AutoKeyParser::ModNameToVk(L"ctrl"),  VK_CONTROL);
    ASSERT_EQ(AutoKeyParser::ModNameToVk(L"Unknown"), 0);

    ASSERT_EQ(AutoKeyParser::ModNameToRegFlag(L"Ctrl"),  MOD_CONTROL);
    ASSERT_EQ(AutoKeyParser::ModNameToRegFlag(L"Shift"), MOD_SHIFT);
    ASSERT_EQ(AutoKeyParser::ModNameToRegFlag(L"Alt"),   MOD_ALT);
    ASSERT_EQ(AutoKeyParser::ModNameToRegFlag(L"Win"),   MOD_WIN);

    printf("  test_ModNameToVk: %d/%d passed\n", g_tests - g_fails, g_tests);
}

//==============================================================================
// Test: ParseSequence
//==============================================================================
static void test_ParseSequence() {
    // Empty input
    {
        auto r = AutoKeyParser::ParseSequence(L"");
        ASSERT_EQ(r.size(), (size_t)0);
    }

    // Single key F13
    {
        auto r = AutoKeyParser::ParseSequence(L"F13");
        ASSERT_EQ(r.size(), (size_t)1);
        ASSERT_CHORD(r[0], 0x7C, {});
    }

    // Single alpha key
    {
        auto r = AutoKeyParser::ParseSequence(L"A");
        ASSERT_EQ(r.size(), (size_t)1);
        ASSERT_CHORD(r[0], 'A', {});
    }

    // Ctrl+A
    {
        auto r = AutoKeyParser::ParseSequence(L"Ctrl+A");
        ASSERT_EQ(r.size(), (size_t)1);
        ASSERT_CHORD(r[0], 'A', {VK_CONTROL});
    }

    // Ctrl+Shift+F13
    {
        auto r = AutoKeyParser::ParseSequence(L"Ctrl+Shift+F13");
        ASSERT_EQ(r.size(), (size_t)1);
        ASSERT_CHORD(r[0], 0x7C, {VK_CONTROL, VK_SHIFT});
    }

    // Multiple chords: Ctrl+A,Ctrl+C
    {
        auto r = AutoKeyParser::ParseSequence(L"Ctrl+A,Ctrl+C");
        ASSERT_EQ(r.size(), (size_t)2);
        ASSERT_CHORD(r[0], 'A', {VK_CONTROL});
        ASSERT_CHORD(r[1], 'C', {VK_CONTROL});
    }

    // Whitespace handling
    {
        auto r = AutoKeyParser::ParseSequence(L" F13 , Ctrl+A ");
        ASSERT_EQ(r.size(), (size_t)2);
        ASSERT_CHORD(r[0], 0x7C, {});
        ASSERT_CHORD(r[1], 'A', {VK_CONTROL});
    }

    printf("  test_ParseSequence: %d/%d passed\n", g_tests - g_fails, g_tests);
}

//==============================================================================
// Test: INI roundtrip (write manually, read with Win32 GetPrivateProfileString)
//==============================================================================
#include <windows.h>

static void test_IniRoundtrip() {
    wchar_t iniPath[MAX_PATH + 1];
    swprintf(iniPath, MAX_PATH, L"D:\\Project\\Tests\\_test_actions.ini");

    DeleteFileW(iniPath);

    // Manually write the INI file (UTF-16 LE with BOM)
    static const unsigned char bom[] = { 0xFF, 0xFE };
    FILE* f = _wfopen(iniPath, L"wb");
    ASSERT_TRUE(f != nullptr);
    if (!f) return;

    fwrite(bom, 1, 2, f);
    auto W = [&](const wchar_t* s) { fwrite(s, 2, wcslen(s), f); };
    auto NL = [&]() { fwrite(L"\r\n", 2, 2, f); };

    W(L"[AutoKey]"); NL();
    W(L"ActionCount=2"); NL();
    NL();
    W(L"[Action_0]"); NL();
    W(L"Name=Action One"); NL();
    W(L"Keys=F13"); NL();
    W(L"Interval=600"); NL();
    W(L"Repeat=0"); NL();
    W(L"ActionMode=1"); NL();
    W(L"TriggerHotkey=Ctrl+F8"); NL();
    W(L"TriggerMode=1"); NL();
    W(L"AutoStart=1"); NL();
    NL();
    W(L"[Action_1]"); NL();
    W(L"Name=Action Two"); NL();
    W(L"Keys=Ctrl+A,Ctrl+C"); NL();
    W(L"Interval=300"); NL();
    W(L"Repeat=5"); NL();
    W(L"ActionMode=0"); NL();
    W(L"TriggerHotkey=F9"); NL();
    W(L"TriggerMode=0"); NL();
    W(L"AutoStart=0"); NL();

    fclose(f);

    // Read back
    wchar_t buf[512] = {};
    int testCount;

    GetPrivateProfileStringW(L"AutoKey", L"ActionCount", L"0", buf, 32, iniPath);
    testCount = _wtoi(buf);
    ASSERT_EQ(testCount, 2);

    // Read Action_0
    GetPrivateProfileStringW(L"Action_0", L"Name", L"", buf, 512, iniPath);
    ASSERT_EQ(std::wstring(buf), std::wstring(L"Action One"));
    GetPrivateProfileStringW(L"Action_0", L"Keys", L"", buf, 512, iniPath);
    ASSERT_EQ(std::wstring(buf), std::wstring(L"F13"));
    GetPrivateProfileStringW(L"Action_0", L"Interval", L"", buf, 512, iniPath);
    ASSERT_EQ(_wtoi(buf), 600);
    GetPrivateProfileStringW(L"Action_0", L"Repeat", L"", buf, 512, iniPath);
    ASSERT_EQ(_wtoi(buf), 0);
    GetPrivateProfileStringW(L"Action_0", L"ActionMode", L"", buf, 512, iniPath);
    ASSERT_EQ(_wtoi(buf), 1);
    GetPrivateProfileStringW(L"Action_0", L"TriggerHotkey", L"", buf, 512, iniPath);
    ASSERT_EQ(std::wstring(buf), std::wstring(L"Ctrl+F8"));
    GetPrivateProfileStringW(L"Action_0", L"TriggerMode", L"", buf, 512, iniPath);
    ASSERT_EQ(_wtoi(buf), 1);
    GetPrivateProfileStringW(L"Action_0", L"AutoStart", L"", buf, 512, iniPath);
    ASSERT_EQ(std::wstring(buf), std::wstring(L"1"));

    // Read Action_1
    GetPrivateProfileStringW(L"Action_1", L"Name", L"", buf, 512, iniPath);
    ASSERT_EQ(std::wstring(buf), std::wstring(L"Action Two"));
    GetPrivateProfileStringW(L"Action_1", L"Keys", L"", buf, 512, iniPath);
    ASSERT_EQ(std::wstring(buf), std::wstring(L"Ctrl+A,Ctrl+C"));
    GetPrivateProfileStringW(L"Action_1", L"Interval", L"", buf, 512, iniPath);
    ASSERT_EQ(_wtoi(buf), 300);
    GetPrivateProfileStringW(L"Action_1", L"Repeat", L"", buf, 512, iniPath);
    ASSERT_EQ(_wtoi(buf), 5);
    GetPrivateProfileStringW(L"Action_1", L"ActionMode", L"", buf, 512, iniPath);
    ASSERT_EQ(_wtoi(buf), 0);
    GetPrivateProfileStringW(L"Action_1", L"TriggerHotkey", L"", buf, 512, iniPath);
    ASSERT_EQ(std::wstring(buf), std::wstring(L"F9"));
    GetPrivateProfileStringW(L"Action_1", L"TriggerMode", L"", buf, 512, iniPath);
    ASSERT_EQ(_wtoi(buf), 0);
    GetPrivateProfileStringW(L"Action_1", L"AutoStart", L"", buf, 512, iniPath);
    ASSERT_EQ(std::wstring(buf), std::wstring(L"0"));

    // Cleanup
    fclose(_wfopen(iniPath, L"ab")); // flush
    DeleteFileW(iniPath);

    printf("  test_IniRoundtrip: %d/%d passed\n", g_tests - g_fails, g_tests);
}

//==============================================================================
// Test: ParseSequence edge cases
//==============================================================================
static void test_ParseSequenceEdge() {
    // Multiple modifiers
    {
        auto r = AutoKeyParser::ParseSequence(L"Ctrl+Alt+Del");
        ASSERT_EQ(r.size(), (size_t)1);
        ASSERT_CHORD(r[0], VK_DELETE, {VK_CONTROL, VK_MENU});
    }

    // Unknown key → default F13
    {
        auto r = AutoKeyParser::ParseSequence(L"UNKNOWN");
        ASSERT_EQ(r.size(), (size_t)1);
        ASSERT_CHORD(r[0], 0x7C, {});
    }

    // Comma-only input (should skip everything)
    {
        auto r = AutoKeyParser::ParseSequence(L",,,");
        ASSERT_EQ(r.size(), (size_t)0);
    }

    printf("  test_ParseSequenceEdge: %d/%d passed\n", g_tests - g_fails, g_tests);
}

//==============================================================================
// Main
//==============================================================================
int main() {
    printf("=== AutoKey Unit Tests ===\n\n");

    test_KeyNameToVk();
    test_ModNameToVk();
    test_ParseSequence();
    test_ParseSequenceEdge();
    test_IniRoundtrip();

    printf("\n=== Results: %d/%d passed ===\n", g_tests - g_fails, g_tests);
    return g_fails > 0 ? 1 : 0;
}
