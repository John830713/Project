#include "AutoKeyModule.h"
#include "AutoKeyDialog.h"
#include "../../Core/ConfigManager.h"
#include "../../Core/DebugConsole.h"
#include "../../Core/Logger.h"
#include "../../Services/TranslationService.h"

//==============================================================================
// Constructor / Configuration
//==============================================================================

AutoKeyModule::AutoKeyModule() {
    InitializeDefinitions();
    InitializeDefaults();
}

AutoKeyModule::~AutoKeyModule() {
    StopAll();
    DestroyHotkeyWindow();
}

void AutoKeyModule::InitializeDefinitions() {
    m_definitions = {
        { L"Enabled", L"Enable Module", ConfigValueType::Bool, L"1", 0, 1 },
    };
}

void AutoKeyModule::InitializeDefaults() {
    for (const auto& def : m_definitions) {
        m_values[def.key] = def.defaultValue;
    }
}

const std::vector<ConfigFieldDefinition>& AutoKeyModule::GetConfigDefinitions() const {
    return m_definitions;
}

bool AutoKeyModule::GetBoolValue(const std::wstring& key, bool defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) return defaultValue;
    return it->second == L"1" || it->second == L"true" || it->second == L"TRUE";
}

std::wstring AutoKeyModule::GetValue(const std::wstring& key) const {
    auto it = m_values.find(key);
    if (it != m_values.end()) return it->second;
    return L"";
}

void AutoKeyModule::SetValue(const std::wstring& key, const std::wstring& value) {
    m_values[key] = value;
}

//==============================================================================
// Lifecycle
//==============================================================================

void AutoKeyModule::Initialize(HINSTANCE hInst, IHostContext* host) {
    m_hInst = hInst;
    m_host = host;

    const wchar_t kClass[] = L"AutoKeyHotkeyClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = HotkeyWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);

    m_hHotkeyWnd = CreateWindowExW(0, kClass, L"", WS_POPUP,
                                   0, 0, 0, 0, nullptr, nullptr, hInst, this);
}

void AutoKeyModule::LoadConfig() {
    m_values = ConfigManager::LoadModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_definitions
    );
    LoadActions();
}

void AutoKeyModule::SaveConfig() {
    ConfigManager::SaveModuleConfig(
        GetConfigFileName(),
        GetConfigSectionName(),
        m_values
    );
    SaveActions();
}

void AutoKeyModule::ApplyConfig() {
    UnregisterHotkeys();

    if (!GetBoolValue(L"Enabled", true)) return;

    for (auto& action : m_actions) {
        if (action.autoStart && action.running == 0) {
            StartAction(action.id);
        }
    }

    RegisterHotkeys();
}

void AutoKeyModule::Shutdown() {
    StopAll();
    Sleep(300);
    UnregisterHotkeys();
    SaveConfig();
}

//==============================================================================
// Actions persistence
//==============================================================================

void AutoKeyModule::LoadActions() {
    m_actions.clear();

    auto configPath = ConfigManager::GetModuleConfigPath(GetConfigFileName());

    wchar_t buf[32] = {};
    GetPrivateProfileStringW(L"AutoKey", L"ActionCount", L"0", buf, 32, configPath.c_str());
    int count = _wtoi(buf);
    if (count <= 0) {
        AutoKeyAction def;
        def.id = 0;
        def.name = L"Anti Sleep";
        def.keys = L"F13";
        def.intervalSeconds = 1200;
        def.autoStart = true;
        m_actions.push_back(std::move(def));
        return;
    }

    for (int i = 0; i < count; ++i) {
        wchar_t sec[64];
        swprintf(sec, 64, L"Action_%d", i);

        wchar_t value[512] = {};

        AutoKeyAction action;
        action.id = i;

        GetPrivateProfileStringW(sec, L"Name", L"", value, 512, configPath.c_str());
        action.name = value;

        GetPrivateProfileStringW(sec, L"Keys", L"F13", value, 512, configPath.c_str());
        action.keys = value;

        GetPrivateProfileStringW(sec, L"Interval", L"1200", value, 512, configPath.c_str());
        action.intervalSeconds = _wtoi(value);
        if (action.intervalSeconds <= 0) action.intervalSeconds = 1200;

        GetPrivateProfileStringW(sec, L"Repeat", L"0", value, 512, configPath.c_str());
        action.repeatCount = _wtoi(value);

        GetPrivateProfileStringW(sec, L"ActionMode", L"1", value, 512, configPath.c_str());
        action.actionMode = _wtoi(value);

        GetPrivateProfileStringW(sec, L"TriggerHotkey", L"", value, 512, configPath.c_str());
        action.triggerHotkey = value;

        GetPrivateProfileStringW(sec, L"TriggerMode", L"0", value, 512, configPath.c_str());
        action.triggerMode = _wtoi(value);

        GetPrivateProfileStringW(sec, L"AutoStart", L"0", value, 512, configPath.c_str());
        action.autoStart = (wcscmp(value, L"1") == 0);

        m_actions.push_back(std::move(action));
    }
}

void AutoKeyModule::SaveActions() {
    auto configPath = ConfigManager::GetModuleConfigPath(GetConfigFileName());

    wchar_t buf[16];
    swprintf(buf, 16, L"%d", static_cast<int>(m_actions.size()));
    WritePrivateProfileStringW(L"AutoKey", L"ActionCount", buf, configPath.c_str());

    for (size_t i = 0; i < m_actions.size(); ++i) {
        wchar_t sec[64];
        swprintf(sec, 64, L"Action_%d", static_cast<int>(i));
        const auto& action = m_actions[i];

        WritePrivateProfileStringW(sec, L"Name", action.name.c_str(), configPath.c_str());
        WritePrivateProfileStringW(sec, L"Keys", action.keys.c_str(), configPath.c_str());

        swprintf(buf, 16, L"%d", action.intervalSeconds);
        WritePrivateProfileStringW(sec, L"Interval", buf, configPath.c_str());

        swprintf(buf, 16, L"%d", action.repeatCount);
        WritePrivateProfileStringW(sec, L"Repeat", buf, configPath.c_str());

        swprintf(buf, 16, L"%d", action.actionMode);
        WritePrivateProfileStringW(sec, L"ActionMode", buf, configPath.c_str());

        WritePrivateProfileStringW(sec, L"TriggerHotkey", action.triggerHotkey.c_str(), configPath.c_str());

        swprintf(buf, 16, L"%d", action.triggerMode);
        WritePrivateProfileStringW(sec, L"TriggerMode", buf, configPath.c_str());

        WritePrivateProfileStringW(sec, L"AutoStart", action.autoStart ? L"1" : L"0", configPath.c_str());
    }
}

//==============================================================================
// Key execution
//==============================================================================

//==============================================================================
// Key execution
//==============================================================================

void AutoKeyModule::ExecuteSequence(const std::vector<ParsedChord>& sequence) {
    for (const auto& chord : sequence) {
        // Press all modifiers down
        for (WORD mod : chord.modifiers) {
            INPUT inp = {};
            inp.type = INPUT_KEYBOARD;
            inp.ki.wVk = mod;
            SendInput(1, &inp, sizeof(INPUT));
        }

        // Press and release main key
        {
            INPUT inp[2] = {};
            inp[0].type = INPUT_KEYBOARD;
            inp[0].ki.wVk = chord.key;
            inp[1].type = INPUT_KEYBOARD;
            inp[1].ki.wVk = chord.key;
            inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, inp, sizeof(INPUT));
        }

        // Release all modifiers (reverse order)
        for (auto it = chord.modifiers.rbegin(); it != chord.modifiers.rend(); ++it) {
            INPUT inp = {};
            inp.type = INPUT_KEYBOARD;
            inp.ki.wVk = *it;
            inp.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &inp, sizeof(INPUT));
        }

        // Small delay between chords in a sequence
        if (&chord != &sequence.back()) {
            Sleep(50);
        }
    }
}

//==============================================================================
// Hotkey system
//==============================================================================

LRESULT CALLBACK AutoKeyModule::HotkeyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_HOTKEY) {
        AutoKeyModule* self = reinterpret_cast<AutoKeyModule*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) {
            self->OnHotkey(static_cast<int>(wParam));
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void AutoKeyModule::OnHotkey(int actionId) {
    if (actionId < 0 || actionId >= static_cast<int>(m_actions.size())) return;

    auto& action = m_actions[actionId];

    if (action.triggerMode == 1) {
        // Toggle mode
        if (action.running != 0) {
            StopAction(actionId);
        } else {
            StartAction(actionId);
        }
    } else {
        // Press mode — execute once
        auto seq = AutoKeyParser::ParseSequence(action.keys);
        if (!seq.empty()) {
            ExecuteSequence(seq);
        }
    }
}

void AutoKeyModule::RegisterHotkeys() {
    if (!m_hHotkeyWnd) return;

    for (const auto& action : m_actions) {
        if (action.triggerHotkey.empty()) continue;

        std::wstring hk = action.triggerHotkey;
        WORD fsModifiers = 0;
        WORD vk = 0;

        size_t plus = hk.find(L'+');
        if (plus != std::wstring::npos) {
            size_t modStart = 0;
            while (modStart < hk.size()) {
                size_t plusEnd = hk.find(L'+', modStart);
                if (plusEnd == std::wstring::npos) {
                    vk = AutoKeyParser::KeyNameToVk(hk.substr(modStart));
                    break;
                }
                std::wstring modName = hk.substr(modStart, plusEnd - modStart);
                WORD flag = AutoKeyParser::ModNameToRegFlag(modName);
                if (flag) fsModifiers |= flag;
                modStart = plusEnd + 1;
            }
        } else {
            vk = AutoKeyParser::KeyNameToVk(hk);
        }

        if (vk) {
            SetWindowLongPtrW(m_hHotkeyWnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(this));
            RegisterHotKey(m_hHotkeyWnd, action.id, fsModifiers, vk);
        }
    }
}

void AutoKeyModule::UnregisterHotkeys() {
    if (!m_hHotkeyWnd) return;
    for (const auto& action : m_actions) {
        UnregisterHotKey(m_hHotkeyWnd, action.id);
    }
}

void AutoKeyModule::DestroyHotkeyWindow() {
    if (m_hHotkeyWnd) {
        DestroyWindow(m_hHotkeyWnd);
        m_hHotkeyWnd = nullptr;
    }
}

//==============================================================================
// Action thread
//==============================================================================

void AutoKeyModule::RunAction(int actionId) {
    if (actionId < 0 || actionId >= static_cast<int>(m_actions.size())) return;

    auto& action = m_actions[actionId];
    auto seq = AutoKeyParser::ParseSequence(action.keys);
    DWORD intervalMs = static_cast<DWORD>(action.intervalSeconds) * 1000;
    int count = 0;

    while (action.running != 0) {
        if (!seq.empty()) {
            ExecuteSequence(seq);
        }

        count++;
        if (action.repeatCount > 0 && count >= action.repeatCount) {
            break;
        }

        DWORD remaining = intervalMs;
        while (remaining > 0 && action.running != 0) {
            DWORD chunk = (remaining > 250) ? 250 : remaining;
            Sleep(chunk);
            remaining -= chunk;
        }
    }

    InterlockedExchange(&action.running, 0);
}

//==============================================================================
// Action control
//==============================================================================

void AutoKeyModule::StartAction(int actionId) {
    if (actionId < 0 || actionId >= static_cast<int>(m_actions.size())) return;

    auto& action = m_actions[actionId];
    if (action.running != 0) return;

    if (action.actionMode == 0) {
        // press_once mode — execute and done
        auto seq = AutoKeyParser::ParseSequence(action.keys);
        if (!seq.empty()) {
            ExecuteSequence(seq);
        }
        return;
    }

    InterlockedExchange(&action.running, 1);
    action.thread = std::thread(&AutoKeyModule::RunAction, this, actionId);
    action.thread.detach();
}

void AutoKeyModule::StopAction(int actionId) {
    if (actionId < 0 || actionId >= static_cast<int>(m_actions.size())) return;

    auto& action = m_actions[actionId];
    InterlockedExchange(&action.running, 0);
}

void AutoKeyModule::StopAll() {
    for (auto& action : m_actions) {
        InterlockedExchange(&action.running, 0);
    }
}

bool AutoKeyModule::IsActionRunning(int actionId) const {
    if (actionId < 0 || actionId >= static_cast<int>(m_actions.size())) return false;
    return m_actions[actionId].running != 0;
}

//==============================================================================
// Context Menu
//==============================================================================

std::vector<ContextMenuItem> AutoKeyModule::GetContextMenuItems() const {
    DBG(L"AutoKeyModule::GetContextMenuItems");
    std::vector<ContextMenuItem> items;

    bool anyRunning = false;
    for (const auto& a : m_actions) {
        if (a.running != 0) { anyRunning = true; break; }
    }

    if (anyRunning) {
        auto label = TranslationService::Get()->Tr(L"AutoKey", L"■ Stop All");
        DBG(L"  StopAll label='%s'", label.c_str());
        items.push_back({ kMenuStopAll, label });
    }

    for (const auto& a : m_actions) {
        if (a.running != 0) {
            auto label = TranslationService::Get()->Tr(L"AutoKey", L"■ Stop") + L" " + a.name;
            DBG(L"  Stop label='%s'", label.c_str());
            items.push_back({ kMenuStopBase + a.id, label });
        } else {
            auto label = TranslationService::Get()->Tr(L"AutoKey", L"▶ Start") + L" " + a.name;
            DBG(L"  Start label='%s'", label.c_str());
            items.push_back({ kMenuStartBase + a.id, label });
        }
    }

    return items;
}

void AutoKeyModule::ExecuteContextMenuItem(int itemId) {
    if (itemId == kMenuStopAll) {
        StopAll();
        return;
    }

    if (itemId >= kMenuStopBase) {
        StopAction(itemId - kMenuStopBase);
    } else {
        StartAction(itemId - kMenuStartBase);
    }
}

//==============================================================================
// Manage Dialog
//==============================================================================

void AutoKeyModule::ShowManageDialog(HWND parent) {
    AutoKeyDialog::Show(parent, this);
}
