#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace {

std::wstring DirectoryOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
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
    if (arguments && argumentCount >= 2) {
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
            CREATE_SUSPENDED,
            nullptr,
            gameDirectory.c_str(),
            &startup,
            &process)) {
        ShowError(L"龙隘之王汉化启动器", WindowsError(L"启动游戏"));
        return 1;
    }

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

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
