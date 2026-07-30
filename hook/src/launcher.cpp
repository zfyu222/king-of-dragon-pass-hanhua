#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::wstring DirectoryOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool HasVisibleWindow(DWORD processId) {
    struct Context {
        DWORD processId;
        bool found;
    } context = {processId, false};
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL {
            auto* context =
                reinterpret_cast<Context*>(parameter);
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner == context->processId &&
                IsWindowVisible(window)) {
                context->found = true;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));
    return context.found;
}

std::vector<DWORD> FindProcessIds(const std::wstring& executableName) {
    std::vector<DWORD> results;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return results;
    }
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, executableName.c_str()) == 0) {
                HANDLE process = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION,
                    FALSE,
                    entry.th32ProcessID);
                DWORD exitCode = 0;
                if (process &&
                    GetExitCodeProcess(process, &exitCode) &&
                    exitCode == STILL_ACTIVE) {
                    results.push_back(entry.th32ProcessID);
                }
                if (process) {
                    CloseHandle(process);
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return results;
}

uintptr_t FindModuleBase(
    DWORD processId, const std::wstring& moduleName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    uintptr_t result = 0;
    MODULEENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, moduleName.c_str()) == 0) {
                result = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

bool InjectDllIntoProcess(
    DWORD processId, const std::wstring& dllPath) {
    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_READ |
            PROCESS_VM_WRITE,
        FALSE,
        processId);
    if (!process) {
        return false;
    }
    const size_t dllBytes =
        (dllPath.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(
        process,
        nullptr,
        dllBytes,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    bool injected =
        remotePath &&
        WriteProcessMemory(
            process,
            remotePath,
            dllPath.c_str(),
            dllBytes,
            nullptr) != FALSE;
    HANDLE thread = nullptr;
    if (injected) {
        auto loadLibrary =
            reinterpret_cast<LPTHREAD_START_ROUTINE>(
                GetProcAddress(
                    GetModuleHandleW(L"kernel32.dll"),
                    "LoadLibraryW"));
        thread = CreateRemoteThread(
            process,
            nullptr,
            0,
            loadLibrary,
            remotePath,
            0,
            nullptr);
        injected =
            thread &&
            WaitForSingleObject(thread, 15000) ==
                WAIT_OBJECT_0;
        DWORD remoteModule = 0;
        injected =
            injected &&
            GetExitCodeThread(thread, &remoteModule) &&
            remoteModule != 0;
    }
    if (thread) {
        CloseHandle(thread);
    }
    if (remotePath) {
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    }
    CloseHandle(process);
    return injected;
}

int RunWatcher(
    const std::wstring& gameExe,
    const std::wstring& dllPath) {
    HMODULE localHook = LoadLibraryExW(
        dllPath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    FARPROC localRefresh =
        localHook ? GetProcAddress(localHook, "KoDPCNRefresh") : nullptr;
    if (!localRefresh && localHook) {
        localRefresh =
            GetProcAddress(localHook, "_KoDPCNRefresh@4");
    }
    if (!localHook || !localRefresh) {
        if (localHook) {
            FreeLibrary(localHook);
        }
        return 2;
    }
    const uintptr_t refreshOffset =
        reinterpret_cast<uintptr_t>(localRefresh) -
        reinterpret_cast<uintptr_t>(localHook);
    FreeLibrary(localHook);

    int consecutiveMisses = 0;
    std::unordered_map<DWORD, DWORD> visibleSince;
    for (;;) {
        const std::vector<DWORD> processIds =
            FindProcessIds(FileNameOf(gameExe));
        if (processIds.empty()) {
            if (++consecutiveMisses > 80) {
                return 0;
            }
            Sleep(250);
            continue;
        }
        bool foundInjectedProcess = false;
        for (const DWORD processId : processIds) {
            uintptr_t moduleBase =
                FindModuleBase(processId, L"KoDPCNHook.dll");
            if (!moduleBase) {
                if (!HasVisibleWindow(processId)) {
                    continue;
                }
                const DWORD now = GetTickCount();
                DWORD& firstVisible = visibleSince[processId];
                if (firstVisible == 0) {
                    firstVisible = now;
                    continue;
                }
                if (now - firstVisible < 2500) {
                    continue;
                }
                InjectDllIntoProcess(processId, dllPath);
                moduleBase =
                    FindModuleBase(processId, L"KoDPCNHook.dll");
            }
            if (!moduleBase) {
                continue;
            }
            foundInjectedProcess = true;
            HANDLE process = OpenProcess(
                PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                    PROCESS_VM_OPERATION | PROCESS_VM_READ |
                    PROCESS_VM_WRITE | SYNCHRONIZE,
                FALSE,
                processId);
            if (process) {
                auto remoteRefresh =
                    reinterpret_cast<LPTHREAD_START_ROUTINE>(
                        moduleBase + refreshOffset);
                HANDLE thread = CreateRemoteThread(
                    process,
                    nullptr,
                    0,
                    remoteRefresh,
                    nullptr,
                    0,
                    nullptr);
                if (thread) {
                    WaitForSingleObject(thread, 2000);
                    CloseHandle(thread);
                }
                CloseHandle(process);
            }
        }
        if (foundInjectedProcess) {
            consecutiveMisses = 0;
        } else if (++consecutiveMisses > 80) {
            return 0;
        }
        Sleep(250);
    }
}

void ShowError(const wchar_t* title, const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), title, MB_OK | MB_ICONERROR);
}

std::wstring WindowsError(const wchar_t* action) {
    const DWORD code = GetLastError();
    wchar_t details[512] = {};
    FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        details,
        static_cast<DWORD>(_countof(details)),
        nullptr);
    return std::wstring(action) + L"失败（错误 " + std::to_wstring(code) +
           L"）：\n" + details;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t selfPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, selfPath, _countof(selfPath));
    const std::wstring root = DirectoryOf(selfPath);
    const std::wstring dllPath = root + L"\\KoDPCNHook.dll";
    const std::wstring iniPath = root + L"\\KoDPCNHook.ini";

    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    std::wstring gameExe;
    if (arguments && argumentCount >= 3 &&
        _wcsicmp(arguments[1], L"--watch") == 0) {
        gameExe = arguments[2];
        LocalFree(arguments);
        return RunWatcher(gameExe, dllPath);
    } else if (arguments && argumentCount >= 2) {
        gameExe = arguments[1];
    } else {
        wchar_t configured[MAX_PATH] = {};
        GetPrivateProfileStringW(
            L"Launcher",
            L"GameExe",
            L"",
            configured,
            _countof(configured),
            iniPath.c_str());
        gameExe = configured;
    }
    if (arguments) {
        LocalFree(arguments);
    }

    if (gameExe.empty()) {
        ShowError(
            L"龙隘之王汉化启动器",
            L"请在 KoDPCNHook.ini 中设置 GameExe，或把游戏 EXE 路径作为第一个参数。");
        return 1;
    }
    if (GetFileAttributesW(gameExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        ShowError(L"龙隘之王汉化启动器", L"找不到游戏：\n" + gameExe);
        return 1;
    }
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        ShowError(L"龙隘之王汉化启动器", L"找不到 Hook DLL：\n" + dllPath);
        return 1;
    }

    const std::wstring gameDirectory = DirectoryOf(gameExe);
    std::wstring commandLine = L"\"" + gameExe + L"\"";
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(
            gameExe.c_str(),
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            gameDirectory.c_str(),
            &startup,
            &process)) {
        ShowError(L"龙隘之王汉化启动器", WindowsError(L"启动游戏"));
        return 1;
    }

    if (false) {
    const size_t dllBytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(
        process.hProcess, nullptr, dllBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool injected = remotePath != nullptr;
    if (injected) {
        injected = WriteProcessMemory(
                       process.hProcess,
                       remotePath,
                       dllPath.c_str(),
                       dllBytes,
                       nullptr) != FALSE;
    }

    HANDLE loadThread = nullptr;
    if (injected) {
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(kernel32, "LoadLibraryW"));
        loadThread = CreateRemoteThread(
            process.hProcess,
            nullptr,
            0,
            loadLibrary,
            remotePath,
            0,
            nullptr);
        injected = loadThread != nullptr;
    }

    DWORD remoteModule = 0;
    if (injected) {
        injected =
            WaitForSingleObject(loadThread, 15000) == WAIT_OBJECT_0 &&
            GetExitCodeThread(loadThread, &remoteModule) &&
            remoteModule != 0;
    }

    if (loadThread) {
        CloseHandle(loadThread);
    }
    if (remotePath) {
        VirtualFreeEx(process.hProcess, remotePath, 0, MEM_RELEASE);
    }

    if (!injected) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        ShowError(
            L"龙隘之王汉化启动器",
            L"Hook DLL 注入失败。请确认启动器、DLL 和游戏均为 32 位，并检查安全软件拦截。");
        return 1;
    }

    // Keep the game entry thread suspended while the injected worker loads the
    // translation database and registers the breakpoint handler.
    Sleep(1500);

    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        ShowError(L"龙隘之王汉化启动器", WindowsError(L"恢复游戏线程"));
        return 1;
    }

    }
    std::wstring watcherCommand =
        L"\"" + std::wstring(selfPath) + L"\" --watch \"" +
        gameExe + L"\"";
    std::vector<wchar_t> mutableWatcher(
        watcherCommand.begin(), watcherCommand.end());
    mutableWatcher.push_back(L'\0');
    STARTUPINFOW watcherStartup = {};
    watcherStartup.cb = sizeof(watcherStartup);
    PROCESS_INFORMATION watcher = {};
    if (CreateProcessW(
            selfPath,
            mutableWatcher.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW | DETACHED_PROCESS,
            nullptr,
            root.c_str(),
            &watcherStartup,
            &watcher)) {
        CloseHandle(watcher.hThread);
        CloseHandle(watcher.hProcess);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
