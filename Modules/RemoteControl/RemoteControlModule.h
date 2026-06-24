#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "../../Core/IFeatureModule.h"
#include "../../Core/IDropActionProvider.h"
#include "../../Core/ConfigTypes.h"
#include "../../Services/FileTransferService.h"
#include "RemoteControlTypes.h"

#include <map>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>

class RemoteControlModule : public IFeatureModule, public IDropActionProvider {
public:
    RemoteControlModule();
    ~RemoteControlModule() override;

    const wchar_t* GetModuleName() const override { return L"RemoteControl"; }
    const wchar_t* GetDisplayName() const override { return L"Remote Control"; }
    const wchar_t* GetVersion() const override { return L"1.0.0"; }
    const wchar_t* GetPurpose() const override {
        return L"Remotely control another computer or allow remote control of this machine."; }
    const wchar_t* GetConfigFileName() const override { return L"Config_RemoteControl.ini"; }
    const wchar_t* GetConfigSectionName() const override { return L"RemoteControl"; }
    const wchar_t* GetLogSourceName() const override { return L"RemoteControl"; }

    const std::vector<ConfigFieldDefinition>& GetConfigDefinitions() const override;

    void Initialize(HINSTANCE hInst, IHostContext* host) override;
    void LoadConfig() override;
    void SaveConfig() override;
    void ApplyConfig() override;
    void Shutdown() override;

    std::wstring GetValue(const std::wstring& key) const override;
    void SetValue(const std::wstring& key, const std::wstring& value) override;

    bool CanHandleDrop(const DropContext& ctx) const override;
    std::vector<DropActionDefinition> GetDropActions(const DropContext& ctx) const override;
    void ExecuteDropAction(int actionId, const DropContext& ctx) override;

    std::vector<ContextMenuItem> GetContextMenuItems() const override;
    void ExecuteContextMenuItem(int itemId) override;

    void StartRemoteControl();
    void StopRemoteControl();
    bool IsRunning() const { return m_running; }
    COLORREF GetLedColor() const;
    void ShowLed(bool visible);

private:
    void InitializeDefinitions();
    void InitializeDefaults();
    bool GetBoolValue(const std::wstring& key, bool defaultValue) const;
    std::wstring GetStringValue(const std::wstring& key, const std::wstring& defaultVal) const;

    void RunAccept();
    void RunSend();
    void RunSvRecv();
    void RunClient();
    void RunLeds();

    static void OnHookEvent(const RemoteControl::Event* e, void* ud);
    static void OnHookToggle(int active, void* ud);

    void EnqueueEvent(std::vector<uint8_t>&& msg);
    void Log(const wchar_t* fmt, ...);
    std::string ToUtf8(const std::wstring& ws) const;

    static LRESULT CALLBACK LedWndProc(HWND h, UINT m, WPARAM w, LPARAM l);
    void CreateLeds();
    void DestroyLeds();
    void UpdateLed();
    void SetLedColor(COLORREF color);
    void UpdateLedPositions();

    enum { kActionStart = 1, kActionStop, kMenuStart = 200, kMenuStop = 201 };

    HINSTANCE m_hInst = nullptr;
    IHostContext* m_host = nullptr;

    std::vector<ConfigFieldDefinition> m_definitions;
    std::map<std::wstring, std::wstring> m_values;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_quit{false};
    SOCKET m_svSocket = INVALID_SOCKET;
    SOCKET m_clSocket = INVALID_SOCKET;
    std::mutex m_svMx;
    std::mutex m_clMx;

    std::thread m_acceptThread;
    std::thread m_sendThread;
    std::thread m_svRecvThread;
    std::thread m_clientThread;
    std::thread m_ledThread;

    std::queue<std::vector<uint8_t>> m_eventQueue;
    std::mutex m_queueMx;
    std::condition_variable m_queueCv;
    volatile LONG m_mouseDx = 0;
    volatile LONG m_mouseDy = 0;
    volatile LONG m_pendingToggleOff = 0;

    FileTransferService::ReceiveState m_ftRx;

    HWND m_led = nullptr;

    // For the single LED: track whether the remote controller has active hook
    volatile LONG m_targetHookActive = 0;
};
