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
#include "../../Core/ILedIndicator.h"
#include "../../Core/ConfigTypes.h"
#include "../../Services/FileTransferService.h"
#include "RemoteControlTypes.h"
#include "RemoteControlDiagnostics.h"

#include <map>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>

class RemoteControlModule : public IFeatureModule, public IDropActionProvider, public ILedIndicator {
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
    void OpenSavePathDialog();
    bool IsRunning() const override { return m_running; }
    COLORREF GetLedColor() const override;
    void ShowLed(bool visible) override;

private:
    void InitializeDefinitions();
    void InitializeDefaults();
    bool GetBoolValue(const std::wstring& key, bool defaultValue) const;
    std::wstring GetStringValue(const std::wstring& key, const std::wstring& defaultVal) const;
    int GetPortValue(const std::wstring& key, int defaultValue) const;
    // ResumeState is an internal bookkeeping flag (not part of
    // m_definitions, so it stays out of the settings UI and SaveConfig):
    // it is read/written straight in the INI by the start/stop menu
    // handlers and ApplyConfig. Shutdown() must never clear it.
    bool ReadResumeState() const;
    void SaveResumeState(bool active) const;

    void RunAccept();
    void RunSend();
    void RunSvRecv();
    void RunClient();
    void RunLeds();

    static void OnHookEvent(const RemoteControl::Event* e, void* ud);
    static void OnHookToggle(int active, void* ud);

    void EnqueueEvent(std::vector<uint8_t>&& msg);
    void Log(const wchar_t* fmt, ...) const;
    std::string ToUtf8(const std::wstring& ws) const;

    // --- Remote diagnostics / deploy backdoor (spec section 10) ---
    // Handled inside both recv loops so the channel works in either
    // direction regardless of F9 control state.
    bool SendToPeer(SOCKET s, std::mutex* mx, const std::vector<uint8_t>& msg);
    bool ForwardToOther(void* fromMx, const std::vector<uint8_t>& frame);
    void CountTx(const std::vector<uint8_t>& framed);
    void WireTap(bool outbound, uint8_t type, uint32_t len);
    void RequestDiagnostics();
    void DeployMyBuild();
    void HandleDiagRequest(uint16_t seq, const char* role, SOCKET s,
                           std::mutex* mx);
    void HandleDiagResponse(const uint8_t* p, uint32_t n);
    void HandleExeResult(const uint8_t* p, uint32_t n);
    void HandleExeSwap(const uint8_t* p, uint32_t n, SOCKET s, std::mutex* mx,
                       const char* role);
    void CheckDiagTimeout();
    void AppendDiagReport(const std::string& header, const std::string& body);
    RemoteControl::Diag::SnapshotData CollectSnapshot(const char* role);

    static LRESULT CALLBACK LedWndProc(HWND h, UINT m, WPARAM w, LPARAM l);
    void CreateLeds();
    void DestroyLeds();
    void UpdateLed();
    void SetLedColor(COLORREF color);
    void UpdateLedPositions();

    enum { kActionStart = 1, kActionStop, kMenuStart = 200, kMenuStop = 201,
           kMenuSelectPath = 202, kMenuDiag = 203, kMenuDeploy = 204 };

    HINSTANCE m_hInst = nullptr;
    IHostContext* m_host = nullptr;

    std::vector<ConfigFieldDefinition> m_definitions;
    // m_values is read by worker threads (GetValue/GetBoolValue/GetPortValue
    // on every received message and at thread start) and written by the UI
    // thread (SetValue from the settings dialog, LoadConfig). std::map has no
    // internal synchronization, so every access goes through this mutex.
    mutable std::mutex m_valuesMx;
    std::map<std::wstring, std::wstring> m_values;

    std::atomic<bool> m_running{false};
    // Generation counter: bumped once by Start and once by Stop. Every worker
    // thread captures the value at spawn and loops while m_epoch == its own,
    // so a Stop/Start cycle can never revive a thread from the previous cycle.
    std::atomic<unsigned> m_epoch{0};
    SOCKET m_svSocket = INVALID_SOCKET;
    // Listening socket lives in RunAccept's stack frame; mirror it here so
    // StopRemoteControl can closesocket() it and wake a blocked accept().
    std::atomic<SOCKET> m_svListen{INVALID_SOCKET};
    SOCKET m_clSocket = INVALID_SOCKET;
    std::mutex m_svMx;
    std::mutex m_clMx;

    std::thread m_acceptThread;
    std::thread m_sendThread;
    std::thread m_svRecvThread;
    std::thread m_clientThread;

    // --- Remote diagnostics / deploy backdoor ---
    // All main workers are detached (epoch-guarded); the counters below are
    // read by CollectSnapshot on a recv thread and written by the send/recv
    // paths, so every access is atomic.
    std::atomic<uint16_t> m_diagSeq{1};
    struct PendingDiag { uint16_t seq; uint64_t tick; };
    std::mutex m_diagMx;                       // guards m_diagPending
    std::vector<PendingDiag> m_diagPending;    // capped, requester side only
    std::atomic<bool> m_deployActive{false};   // one EXE_SWAP worker at a time
    std::atomic<uint64_t> m_startTick{0};      // uptime base (Initialize)
    std::atomic<uint64_t> m_txPkt{0}, m_txBytes{0};
    std::atomic<uint64_t> m_rxPkt{0}, m_rxBytes{0};
    std::atomic<uint64_t> m_lastTxTick{0}, m_lastRxTick{0};
    // Worker liveness mirrors for the snapshot (set/cleared by RAII in the
    // thread entry points).
    std::atomic<bool> m_thrAccept{false};
    std::atomic<bool> m_thrSend{false};
    std::atomic<bool> m_thrSvRecv{false};
    std::atomic<bool> m_thrClient{false};
    std::atomic<bool> m_thrLed{false};

    std::queue<std::vector<uint8_t>> m_eventQueue;
    std::mutex m_queueMx;
    std::condition_variable m_queueCv;
    volatile LONG m_mouseDx = 0;
    volatile LONG m_mouseDy = 0;
    volatile LONG m_pendingToggleOff = 0;

    // m_ftRx is touched by three threads: RunAccept resets it on a new
    // client, RunSvRecv feeds it ProcessMsg, and StopRemoteControl resets it
    // from the UI thread. ResetRecv closes an open file handle, so an
    // unsynchronized overlap is a double-close / use-after-free.
    std::mutex m_ftRxMx;
    FileTransferService::ReceiveState m_ftRx;

    // LED window handle, its mover thread, and every accessor are guarded by
    // m_ledMx. The thread is joinable (joined by DestroyLeds) so it can never
    // touch the HWND while the UI thread destroys it.
    mutable std::mutex m_ledMx;
    std::thread m_ledThread;
    HWND m_led = nullptr;

    // For the single LED: track whether the remote controller has active hook
    volatile LONG m_targetHookActive = 0;
};
