#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace {

using CreateFileAType = HANDLE(WINAPI*)(
    LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileWType = HANDLE(WINAPI*)(
    LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using WriteFileType = BOOL(WINAPI*)(
    HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
using ReadFileType = BOOL(WINAPI*)(
    HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using VirtualProtectType = BOOL(WINAPI*)(LPVOID, SIZE_T, DWORD, PDWORD);
using VirtualAllocType = LPVOID(WINAPI*)(LPVOID, SIZE_T, DWORD, DWORD);
using MapViewOfFileType = LPVOID(WINAPI*)(
    HANDLE, DWORD, DWORD, DWORD, SIZE_T);
using GetProcAddressType = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using SleepType = VOID(WINAPI*)(DWORD);
using GetTickCountType = DWORD(WINAPI*)();
using QueryPerformanceCounterType = BOOL(WINAPI*)(LARGE_INTEGER*);
using DispatchMessageAType = LRESULT(WINAPI*)(const MSG*);
using MultiByteToWideCharType = int(WINAPI*)(
    UINT, DWORD, LPCCH, int, LPWSTR, int);
using DrawTextAType = int(WINAPI*)(HDC, LPCSTR, int, LPRECT, UINT);

CreateFileAType g_originalCreateFileA = nullptr;
CreateFileWType g_originalCreateFileW = nullptr;
WriteFileType g_originalWriteFile = nullptr;
ReadFileType g_originalReadFile = nullptr;
VirtualProtectType g_originalVirtualProtect = nullptr;
VirtualAllocType g_originalVirtualAlloc = nullptr;
MapViewOfFileType g_originalMapViewOfFile = nullptr;
GetProcAddressType g_originalGetProcAddress = nullptr;
SleepType g_originalSleep = nullptr;
GetTickCountType g_originalGetTickCount = nullptr;
QueryPerformanceCounterType g_originalQueryPerformanceCounter = nullptr;
DispatchMessageAType g_originalDispatchMessageA = nullptr;
MultiByteToWideCharType g_originalMultiByteToWideChar = nullptr;
DrawTextAType g_originalDrawTextA = nullptr;
wchar_t g_hookDirectory[MAX_PATH] = {};
LONG g_redirectLogWritten = 0;
LONG g_translationLogCount = 0;
LONG g_translationsReady = 0;
PVOID g_vectoredHandler = nullptr;
HANDLE g_temporaryS86WriteHandle = INVALID_HANDLE_VALUE;
HANDLE g_temporaryS86ReadHandle = INVALID_HANDLE_VALUE;
LONG g_s86ProbeBytesPatched = 0;
LONG g_runtimeHooksInstalled = 0;
unsigned char* g_candidateRuntimeBase = nullptr;
uintptr_t g_hardwareTargets[4] = {};
LONG g_hardwareBreakpointsReady = 0;
DWORD g_earlyTargetThreadId = 0;
LONG g_earlyBreakpointsApplied = 0;
LONG g_runtimeStringsPatched = 0;
LONG g_captionPrepareCount = 0;
LONG g_captionDrawCount = 0;
void* g_uiElements[4096] = {};
LONG g_uiElementCount = 0;
struct RuntimeAllocation {
    unsigned char* address;
    size_t bytes;
};
RuntimeAllocation g_runtimeAllocations[64] = {};
LONG g_runtimeAllocationCount = 0;
std::unordered_map<std::string, std::string> g_translations;
std::unordered_map<std::string, std::string> g_normalizedTranslations;
std::unordered_set<std::string> g_translatedValues;
using TranslationEntry =
    std::pair<const std::string, std::string>;
std::vector<const TranslationEntry*>
    g_compositeFragmentsByFirstByte[256];
std::unordered_map<void*, void*> g_externalFontReplacements;
std::unordered_map<int, void*> g_externalFontsByPointSize;
std::unordered_map<void*, std::string> g_nativePreparedCaptions;
std::vector<unsigned char> g_externalFontData;

struct OverlayCaption {
    std::wstring text;
    int x;
    int y;
    int width;
    int height;
    int fontHeight;
    int clipLeft;
    int clipTop;
    int clipRight;
    int clipBottom;
    DWORD updatedAt;
};

SRWLOCK g_overlayLock = SRWLOCK_INIT;
std::vector<OverlayCaption> g_overlayCaptions;
HWND g_gameWindow = nullptr;
HWND g_overlayWindow = nullptr;
HANDLE g_overlayThread = nullptr;
const wchar_t kOverlayClassName[] = L"KoDPCNCaptionOverlay";
const COLORREF kOverlayKeyColor = RGB(255, 0, 255);
// 仅当 caption 数据真正变化或窗口移动时才请求重绘，避免每帧整窗
// FillRect + 重画导致 color-key layered 窗口在 DWM 下持续闪烁。
LONG g_overlayDirty = 0;
RECT g_overlayWindowRect = {};
BOOL g_overlayWindowVisible = FALSE;

struct FormatPattern {
    std::vector<std::string> literals;
    std::vector<std::string> tokens;
    std::string translated;
    bool translateCaptures = false;
};
std::vector<FormatPattern> g_formatPatterns;
std::vector<FormatPattern> g_opalFormatPatternsByFirstByte[256];

void WriteLog(const wchar_t* message);
const char* TranslateRuntimeText(const char* source);
void LogTranslationHit(const char* source, const char* translated);
void TranslatePropertyHolder(void* destination);
bool IsReadableCode(const unsigned char* address, size_t length);
int ApplyHardwareBreakpointsToGameThreads();
DWORD WINAPI InstallEarlyBreakpoints(void*);
void EnsureRuntimeHooksFromGameThread();

using AdvisorTextType = int*(__thiscall*)(void*, const char*, int);
using CaptionPrepareType =
    int(__thiscall*)(void*, int, int*, char);
using CaptionDrawType = int(__thiscall*)(void*, int);
using PrimitiveQueueType =
    int(__thiscall*)(void*, void*, int);
using UIFactoryType = void*(__cdecl*)();
using GetCaptionType = const char*(__thiscall*)(void*);
using SetCaptionType = int(__thiscall*)(void*, const char*);
using TextBuilderType = void*(__thiscall*)(void*, const char*);
using PropertyStringType =
    char(__thiscall*)(void*, const char*, void*, int, int);
using SetPropertyStringType = void*(__thiscall*)(void*, const char*);
using PropertySerialiseType = int(__thiscall*)(void*);
using HashStringType = uint32_t(__cdecl*)(const char*);
using ResolveResourceType = int(__cdecl*)(int*, const char*, int);
using FontSerialiseType = short(__thiscall*)(void*);
using FontPrepareStringType =
    void(__cdecl*)(int, const char*, unsigned int);
AdvisorTextType g_originalAdvisorText = nullptr;
CaptionPrepareType g_originalCaptionPrepare = nullptr;
CaptionDrawType g_originalCaptionDraw = nullptr;
PrimitiveQueueType g_originalPrimitiveQueue = nullptr;
UIFactoryType g_originalLabelFactory = nullptr;
UIFactoryType g_originalButtonFactory = nullptr;
TextBuilderType g_originalBuilderStart = nullptr;
TextBuilderType g_originalBuilderAppend = nullptr;
PropertyStringType g_originalPropertyString = nullptr;
SetPropertyStringType g_setPropertyString = nullptr;
PropertySerialiseType g_originalPropertySerialise = nullptr;
uint32_t g_captionPropertyHash = 0;
ResolveResourceType g_originalResolveResource = nullptr;
FontSerialiseType g_originalFontSerialise = nullptr;
FontPrepareStringType g_originalFontPrepareString = nullptr;
void* g_originalFontFinalizeTail = nullptr;
unsigned char* g_runtimeImageBase = nullptr;
LONG g_fontOverrideLogCount = 0;
thread_local int g_captionVisibilityProbeDepth = 0;
thread_local bool g_captionVisibilityAccepted = false;

std::wstring ToLowerPath(const wchar_t* path) {
    std::wstring result(path ? path : L"");
    for (wchar_t& ch : result) {
        if (ch == L'/') {
            ch = L'\\';
        } else if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
    }
    return result;
}

bool EndsWith(const std::wstring& value, const wchar_t* suffix) {
    const size_t suffixLength = std::wcslen(suffix);
    return value.size() >= suffixLength &&
           value.compare(value.size() - suffixLength, suffixLength, suffix) == 0;
}

bool GetOverlayPath(
    const wchar_t* requestedPath,
    DWORD desiredAccess,
    wchar_t* overlayPath,
    size_t overlayCapacity) {
    if (!requestedPath || (desiredAccess & (GENERIC_WRITE | DELETE)) != 0) {
        return false;
    }
    const std::wstring normalized = ToLowerPath(requestedPath);
    size_t marker = normalized.rfind(L"\\kodp\\");
    size_t relativeStart = std::wstring::npos;
    if (marker != std::wstring::npos) {
        relativeStart = marker + 1;
    } else if (normalized.rfind(L"kodp\\", 0) == 0) {
        relativeStart = 0;
    } else {
        const size_t slash = normalized.find_last_of(L'\\');
        const std::wstring fileName =
            slash == std::wstring::npos
                ? normalized
                : normalized.substr(slash + 1);
        if (fileName == L"kodp.group.bin" ||
            fileName == L"kodp1024.group.bin" ||
            fileName == L"kodp480.group.bin" ||
            fileName == L"kodp_res.group.bin") {
            relativeStart = slash == std::wstring::npos ? 0 : slash + 1;
        }
    }
    if (relativeStart == std::wstring::npos) {
        return false;
    }
    const std::wstring relative = normalized.substr(relativeStart);
    // The illustrated lore/manual pages are rendered by the embedded HTML
    // viewer and never enter Marmalade's caption/font pipeline.  Redirect only
    // those translated HTML documents; keep scenes, UI, plist and group files
    // on the native runtime-hook path so their layout behavior is unchanged.
    const bool isTranslatedHtml =
        EndsWith(relative, L".html") &&
        (relative.rfind(L"kodp\\text\\", 0) == 0 ||
         relative.rfind(L"kodp\\manual\\", 0) == 0);
    if (!isTranslatedHtml) {
        return false;
    }
    const int written = _snwprintf_s(
        overlayPath,
        overlayCapacity,
        _TRUNCATE,
        L"%s\\overlay\\%s",
        g_hookDirectory,
        relative.c_str());
    if (written <= 0 || GetFileAttributesW(overlayPath) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return true;
}

bool IsTemporaryS86(const wchar_t* path) {
    if (!path) {
        return false;
    }
    const std::wstring normalized = ToLowerPath(path);
    if (!EndsWith(normalized, L"king of dragon pass.s86")) {
        return false;
    }
    wchar_t temporaryDirectory[MAX_PATH] = {};
    if (GetTempPathW(_countof(temporaryDirectory), temporaryDirectory) == 0) {
        return false;
    }
    const std::wstring normalizedTemporary = ToLowerPath(temporaryDirectory);
    return normalized.compare(0, normalizedTemporary.size(), normalizedTemporary) == 0;
}

bool IsGameS86(const wchar_t* path) {
    return path &&
           EndsWith(
               ToLowerPath(path),
               L"king of dragon pass.s86");
}

bool PatchTemporaryS86(const wchar_t* path) {
    static LONG patched = 0;
    if (!IsTemporaryS86(path) ||
        InterlockedCompareExchange(&patched, 1, 0) != 0) {
        return false;
    }
    HANDLE file = g_originalCreateFileW(
        path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        InterlockedExchange(&patched, 0);
        WriteLog(L"failed to open the temporary s86 for breakpoint patching");
        return false;
    }
    static const DWORD offsets[] = {
        0x9227A, 0x10A870, 0x160830, 0x160950,
    };
    bool success = true;
    for (DWORD offset : offsets) {
        LARGE_INTEGER position = {};
        position.QuadPart = offset;
        if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {
            success = false;
            break;
        }
        BYTE original = 0;
        DWORD transferred = 0;
        if (!ReadFile(file, &original, 1, &transferred, nullptr) ||
            transferred != 1 || original != 0x55 ||
            !SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {
            success = false;
            break;
        }
        const BYTE breakpoint = 0xCC;
        if (!WriteFile(file, &breakpoint, 1, &transferred, nullptr) ||
            transferred != 1) {
            success = false;
            break;
        }
    }
    FlushFileBuffers(file);
    CloseHandle(file);
    WriteLog(
        success
            ? L"patched four breakpoint probes into the temporary s86 copy"
            : L"failed to patch the temporary s86 copy");
    return success;
}

bool PatchTemporaryS86Handle(HANDLE file) {
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER originalPosition = {};
    LARGE_INTEGER zero = {};
    if (!SetFilePointerEx(file, zero, &originalPosition, FILE_CURRENT)) {
        return false;
    }
    static const DWORD offsets[] = {
        0x9227A, 0x10A870, 0x160830, 0x160950,
    };
    bool success = true;
    for (DWORD offset : offsets) {
        LARGE_INTEGER position = {};
        position.QuadPart = offset;
        BYTE original = 0;
        DWORD transferred = 0;
        if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN) ||
            !ReadFile(file, &original, 1, &transferred, nullptr) ||
            transferred != 1 || original != 0x55 ||
            !SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {
            success = false;
            break;
        }
        const BYTE breakpoint = 0xCC;
        if (!g_originalWriteFile(
                file, &breakpoint, 1, &transferred, nullptr) ||
            transferred != 1) {
            success = false;
            break;
        }
    }
    SetFilePointerEx(file, originalPosition, nullptr, FILE_BEGIN);
    FlushFileBuffers(file);
    WriteLog(
        success
            ? L"patched four breakpoint probes in the opened temporary s86"
            : L"failed to patch the opened temporary s86");
    return success;
}

BOOL WINAPI HookWriteFile(
    HANDLE file,
    LPCVOID buffer,
    DWORD bytesToWrite,
    LPDWORD bytesWritten,
    LPOVERLAPPED overlapped) {
    if (file != g_temporaryS86WriteHandle || !buffer || bytesToWrite == 0 ||
        overlapped) {
        return g_originalWriteFile(
            file, buffer, bytesToWrite, bytesWritten, overlapped);
    }
    LARGE_INTEGER zero = {};
    LARGE_INTEGER position = {};
    if (!SetFilePointerEx(file, zero, &position, FILE_CURRENT)) {
        return g_originalWriteFile(
            file, buffer, bytesToWrite, bytesWritten, overlapped);
    }
    static const DWORD offsets[] = {
        0x9227A, 0x10A870, 0x160830, 0x160950,
    };
    std::vector<unsigned char> patched;
    for (DWORD offset : offsets) {
        if (offset < static_cast<ULONGLONG>(position.QuadPart) ||
            offset >= static_cast<ULONGLONG>(position.QuadPart) + bytesToWrite) {
            continue;
        }
        if (patched.empty()) {
            const auto* source = static_cast<const unsigned char*>(buffer);
            patched.assign(source, source + bytesToWrite);
        }
        const size_t index =
            static_cast<size_t>(offset - position.QuadPart);
        if (patched[index] == 0x55) {
            patched[index] = 0xCC;
            InterlockedIncrement(&g_s86ProbeBytesPatched);
        }
    }
    const void* output = patched.empty() ? buffer : patched.data();
    const BOOL result = g_originalWriteFile(
        file, output, bytesToWrite, bytesWritten, overlapped);
    if (InterlockedCompareExchange(&g_s86ProbeBytesPatched, 0, 0) == 4) {
        static LONG logged = 0;
        if (InterlockedCompareExchange(&logged, 1, 0) == 0) {
            WriteLog(L"patched four breakpoint probes while creating the temporary s86");
        }
    }
    return result;
}

BOOL WINAPI HookReadFile(
    HANDLE file,
    LPVOID buffer,
    DWORD bytesToRead,
    LPDWORD bytesRead,
    LPOVERLAPPED overlapped) {
    return g_originalReadFile(
        file, buffer, bytesToRead, bytesRead, overlapped);
}

int WINAPI HookMultiByteToWideChar(
    UINT codePage,
    DWORD flags,
    LPCCH source,
    int sourceBytes,
    LPWSTR destination,
    int destinationCharacters) {
    if (!source || sourceBytes == 0 ||
        InterlockedCompareExchange(&g_translationsReady, 0, 0) == 0) {
        return g_originalMultiByteToWideChar(
            codePage,
            flags,
            source,
            sourceBytes,
            destination,
            destinationCharacters);
    }
    const size_t length =
        sourceBytes < 0 ? std::strlen(source) : static_cast<size_t>(sourceBytes);
    if (length == 0 || length > 65535) {
        return g_originalMultiByteToWideChar(
            codePage,
            flags,
            source,
            sourceBytes,
            destination,
            destinationCharacters);
    }
    const std::string text(source, length);
    static LONG observedConversionCount = 0;
    if (length >= 3 && length <= 300 &&
        InterlockedIncrement(&observedConversionCount) <= 60) {
        const int needed = g_originalMultiByteToWideChar(
            codePage, 0, text.c_str(), static_cast<int>(text.size()),
            nullptr, 0);
        if (needed > 0 && needed <= 400) {
            std::wstring wide(static_cast<size_t>(needed), L'\0');
            g_originalMultiByteToWideChar(
                codePage, 0, text.c_str(), static_cast<int>(text.size()),
                wide.data(), needed);
            const std::wstring message =
                L"observed MultiByteToWideChar text: " + wide;
            WriteLog(message.c_str());
        }
    }
    const char* translated = TranslateRuntimeText(text.c_str());
    if (translated == text.c_str()) {
        return g_originalMultiByteToWideChar(
            codePage,
            flags,
            source,
            sourceBytes,
            destination,
            destinationCharacters);
    }
    LogTranslationHit(text.c_str(), translated);
    const int translatedBytes =
        sourceBytes < 0 ? -1 : static_cast<int>(std::strlen(translated));
    return g_originalMultiByteToWideChar(
        CP_UTF8,
        flags,
        translated,
        translatedBytes,
        destination,
        destinationCharacters);
}

int WINAPI HookDrawTextA(
    HDC dc, LPCSTR source, int sourceBytes, LPRECT rectangle, UINT format) {
    if (!source || sourceBytes == 0 ||
        InterlockedCompareExchange(&g_translationsReady, 0, 0) == 0) {
        return g_originalDrawTextA(
            dc, source, sourceBytes, rectangle, format);
    }
    const size_t length =
        sourceBytes < 0 ? std::strlen(source) : static_cast<size_t>(sourceBytes);
    const std::string text(source, length);
    static LONG observedDrawTextCount = 0;
    if (length >= 3 && length <= 300 &&
        InterlockedIncrement(&observedDrawTextCount) <= 60) {
        const int needed = g_originalMultiByteToWideChar(
            CP_ACP, 0, text.c_str(), static_cast<int>(text.size()),
            nullptr, 0);
        if (needed > 0 && needed <= 400) {
            std::wstring wide(static_cast<size_t>(needed), L'\0');
            g_originalMultiByteToWideChar(
                CP_ACP, 0, text.c_str(), static_cast<int>(text.size()),
                wide.data(), needed);
            const std::wstring message =
                L"observed DrawTextA text: " + wide;
            WriteLog(message.c_str());
        }
    }
    const char* translated = TranslateRuntimeText(text.c_str());
    if (translated == text.c_str()) {
        return g_originalDrawTextA(
            dc, source, sourceBytes, rectangle, format);
    }
    LogTranslationHit(text.c_str(), translated);
    return g_originalDrawTextA(
        dc,
        translated,
        sourceBytes < 0 ? -1 : static_cast<int>(std::strlen(translated)),
        rectangle,
        format);
}

void WriteLog(const wchar_t* message) {
    if (!g_originalCreateFileW || !message) {
        return;
    }
    wchar_t logPath[MAX_PATH] = {};
    _snwprintf_s(
        logPath, _countof(logPath), _TRUNCATE, L"%s\\KoDPCNHook.log", g_hookDirectory);
    HANDLE file = g_originalCreateFileW(
        logPath,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME time = {};
    GetLocalTime(&time);
    wchar_t line[2048] = {};
    _snwprintf_s(
        line,
        _countof(line),
        _TRUNCATE,
        L"[%04u-%02u-%02u %02u:%02u:%02u] %s\r\n",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        message);
    const int needed =
        WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    if (needed > 1) {
        std::string utf8(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, line, -1, utf8.data(), needed, nullptr, nullptr);
        utf8.resize(static_cast<size_t>(needed - 1));
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    CloseHandle(file);
}

std::string UnescapeCorpusText(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            result.push_back(text[i]);
            continue;
        }
        const char next = text[i + 1];
        if (next == '\\') {
            result.push_back('\\');
            ++i;
        } else if (next == 'n') {
            result.push_back('\n');
            ++i;
        } else if (next == 'r') {
            result.push_back('\r');
            ++i;
        } else if (next == 't') {
            result.push_back('\t');
            ++i;
        } else if (
            next == 'x' && i + 3 < text.size() &&
            std::isxdigit(static_cast<unsigned char>(text[i + 2])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 3]))) {
            const char digits[3] = {text[i + 2], text[i + 3], '\0'};
            result.push_back(static_cast<char>(std::strtoul(digits, nullptr, 16)));
            i += 3;
        } else {
            result.push_back('\\');
        }
    }
    return result;
}

bool IsPrintfConversion(char ch) {
    return std::strchr("diuoxXfFeEgGaAcspn%", ch) != nullptr;
}

bool ParseFormatPattern(
    const std::string& source,
    const std::string& translated,
    FormatPattern* pattern) {
    pattern->literals.clear();
    pattern->tokens.clear();
    pattern->translated = translated;
    size_t cursor = 0;
    size_t literalStart = 0;
    while (cursor < source.size()) {
        if (source[cursor] != '%') {
            ++cursor;
            continue;
        }
        size_t end = cursor + 1;
        while (end < source.size() && end - cursor <= 32) {
            if (IsPrintfConversion(source[end])) {
                if (source[end] == '%') {
                    cursor = end + 1;
                    break;
                }
                pattern->literals.push_back(source.substr(literalStart, cursor - literalStart));
                pattern->tokens.push_back(source.substr(cursor, end - cursor + 1));
                cursor = end + 1;
                literalStart = cursor;
                break;
            }
            ++end;
        }
        if (end >= source.size() || end - cursor > 32) {
            ++cursor;
        }
    }
    if (pattern->tokens.empty()) {
        return false;
    }
    pattern->literals.push_back(source.substr(literalStart));
    size_t literalBytes = 0;
    for (const std::string& literal : pattern->literals) {
        literalBytes += literal.size();
    }
    return literalBytes >= 3;
}

bool BuildOpalRuntimePattern(
    const std::string& source,
    const std::string& translated,
    FormatPattern* pattern) {
    auto replaceMarkers = [](
        const std::string& input,
        std::string* output,
        std::vector<std::string>* markers) {
        output->clear();
        markers->clear();
        for (size_t cursor = 0; cursor < input.size();) {
            if (cursor + 2 < input.size() &&
                static_cast<unsigned char>(input[cursor]) == 0x01 &&
                input[cursor + 1] == '@') {
                markers->push_back(input.substr(cursor, 3));
                output->append("%s");
                cursor += 3;
                continue;
            }
            output->push_back(input[cursor++]);
        }
    };

    std::string runtimeSource;
    std::string runtimeTranslation;
    std::vector<std::string> sourceMarkers;
    std::vector<std::string> translatedMarkers;
    replaceMarkers(source, &runtimeSource, &sourceMarkers);
    replaceMarkers(translated, &runtimeTranslation, &translatedMarkers);
    if (sourceMarkers.empty() || sourceMarkers != translatedMarkers ||
        runtimeSource.empty() || runtimeSource.front() == '%') {
        return false;
    }
    if (!ParseFormatPattern(
            runtimeSource, runtimeTranslation, pattern)) {
        return false;
    }
    size_t literalBytes = 0;
    for (const std::string& literal : pattern->literals) {
        literalBytes += literal.size();
    }
    if (literalBytes < 20) {
        return false;
    }
    pattern->translateCaptures = true;
    return true;
}

bool LoadTranslations() {
    const std::wstring databasePath =
        std::wstring(g_hookDirectory) + L"\\translations.tsv";
    std::ifstream input(databasePath, std::ios::binary);
    if (!input) {
        WriteLog(L"translations.tsv was not found");
        return false;
    }
    std::string line;
    std::vector<std::pair<std::string, std::string>> knownForAliases;
    std::vector<std::pair<std::string, std::string>> leadingSpaceAliases;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        std::string source = UnescapeCorpusText(line.substr(0, tab));
        std::string translated = UnescapeCorpusText(line.substr(tab + 1));
        if (source.empty() || source == translated) {
            continue;
        }
        size_t sourceLeading = 0;
        while (sourceLeading < source.size() &&
               std::isspace(static_cast<unsigned char>(
                   source[sourceLeading]))) {
            ++sourceLeading;
        }
        size_t translatedLeading = 0;
        while (translatedLeading < translated.size() &&
               std::isspace(static_cast<unsigned char>(
                   translated[translatedLeading]))) {
            ++translatedLeading;
        }
        if (sourceLeading > 0 && sourceLeading < source.size()) {
            leadingSpaceAliases.emplace_back(
                source.substr(sourceLeading),
                translated.substr(translatedLeading));
        }
        g_translations.emplace(source, translated);
        std::string normalizedSource;
        normalizedSource.reserve(source.size());
        bool pendingSpace = false;
        for (unsigned char ch : source) {
            if (std::isspace(ch)) {
                pendingSpace = !normalizedSource.empty();
                continue;
            }
            if (pendingSpace) {
                normalizedSource.push_back(' ');
                pendingSpace = false;
            }
            normalizedSource.push_back(static_cast<char>(ch));
        }
        if (!normalizedSource.empty()) {
            g_normalizedTranslations.emplace(
                std::move(normalizedSource), translated);
        }
        g_translatedValues.insert(translated);
        FormatPattern pattern;
        if (ParseFormatPattern(source, translated, &pattern)) {
            g_formatPatterns.push_back(std::move(pattern));
        }
        FormatPattern opalPattern;
        if (BuildOpalRuntimePattern(
                source, translated, &opalPattern)) {
            const unsigned char first = static_cast<unsigned char>(
                opalPattern.literals.front().front());
            g_opalFormatPatternsByFirstByte[first].push_back(
                std::move(opalPattern));
        }

        // Clan descriptions are stored with a three-byte control prefix and
        // later spliced into "They're known for ..." at runtime.  Derive the
        // visible fragment automatically so every clan uses the translated
        // database text, including descriptions added in other scenarios.
        const std::string knownForMarker = ", known for ";
        const size_t knownFor = source.find(knownForMarker);
        if (knownFor != std::string::npos) {
            std::string visibleSource =
                source.substr(knownFor + knownForMarker.size());
            std::string visibleTranslation = translated;
            if (visibleTranslation.size() >= 3 &&
                static_cast<unsigned char>(visibleTranslation[0]) == 0x01 &&
                visibleTranslation[1] == '@') {
                visibleTranslation.erase(0, 3);
            }
            const std::string chinesePossessive = u8"以其";
            if (visibleTranslation.compare(
                    0,
                    chinesePossessive.size(),
                    chinesePossessive) == 0) {
                visibleTranslation.erase(0, chinesePossessive.size());
            }
            if (!visibleSource.empty() && !visibleTranslation.empty()) {
                knownForAliases.emplace_back(
                    visibleSource, visibleTranslation);
                if (visibleSource.compare(0, 4, "its ") == 0) {
                    knownForAliases.emplace_back(
                        visibleSource.substr(4), visibleTranslation);
                }
            }
        }
    }

    for (const auto& alias : knownForAliases) {
        g_translations.insert_or_assign(alias.first, alias.second);
        g_translations.insert_or_assign(
            alias.first + ".", alias.second + u8"。");
        g_translatedValues.insert(alias.second);
        g_translatedValues.insert(alias.second + u8"。");
    }
    for (const auto& alias : leadingSpaceAliases) {
        g_translations.insert_or_assign(alias.first, alias.second);
        g_translatedValues.insert(alias.second);
    }
    // This source prefix is grammatically completed by the derived Chinese
    // description above (for example: 他们因 + 巨石……而闻名).
    g_translations.insert_or_assign("They're known for ", u8"他们因");
    g_normalizedTranslations.insert_or_assign(
        "They're known for", u8"他们因");
    g_translatedValues.insert(u8"他们因");
    const std::pair<const char*, const char*> runtimePatterns[] = {
        {
            "The pigs need %d hides of wildland, and the other livestock "
            "need to graze on %d hides of land. They need %d herders to "
            "tend them.",
            u8"猪群需要 %d 张兽皮的荒地，其他牲畜需要在 %d 张兽皮的土地上放牧。"
            u8"它们需要 %d 名牧人照料。",
        },
        {
            "We need %d farmers and %d oxen to work all of our cropland. "
            "We have %d healthy farmers, and %d of our cattle are strong "
            "young oxen.",
            u8"我们需要 %d 名农夫和 %d 头耕牛来耕种全部农田。"
            u8"目前有 %d 名健康农夫，另有 %d 头牛是强壮的年轻耕牛。",
        },
        {
            "The pigs need %d hides of wildland, and the other livestock "
            "need to graze on %d hides of land. They need %d herder to "
            "tend them.",
            u8"猪群需要 %d 张兽皮的荒地，其他牲畜需要在 %d 张兽皮的土地上放牧。"
            u8"它们需要 %d 名牧人照料。",
        },
    };
    for (const auto& runtimePattern : runtimePatterns) {
        FormatPattern pattern;
        if (ParseFormatPattern(
                runtimePattern.first,
                runtimePattern.second,
                &pattern)) {
            g_formatPatterns.push_back(std::move(pattern));
        }
    }
    for (const auto& entry : g_translations) {
        if (entry.first.size() < 2 ||
            entry.first.find('%') != std::string::npos) {
            continue;
        }
        const unsigned char first =
            static_cast<unsigned char>(entry.first.front());
        g_compositeFragmentsByFirstByte[first].push_back(&entry);
    }
    for (auto& fragments : g_compositeFragmentsByFirstByte) {
        std::sort(
            fragments.begin(),
            fragments.end(),
            [](const TranslationEntry* left,
               const TranslationEntry* right) {
                return left->first.size() > right->first.size();
            });
    }
    const char* pigCompositeProbe =
        "The pigs need 23 hides of wildland, and the other livestock "
        "need to graze on 27 hides of land. They need 62 herders to "
        "tend them.";
    const char* farmerCompositeProbe =
        "We need 238 farmers and 210 oxen to work all of our cropland. "
        "We have 411 healthy farmers, and 265 of our cattle are strong "
        "young oxen.";
    const char* genericCompositeProbe =
        "We need 1 farmer and 1 ox to work all of our cropland. "
        "We have 1 healthy farmers, and 1 of our cattle are strong "
        "young oxen.";
    const char* textOnlyCompositeProbe =
        "farmers and oxen";
    const char* clanSuffixProbe = "Bachad N";
    const char* clanDescriptionProbe =
        "They're known for a large boulder with war powers. "
        "Their chief is Fantarl. They owe us a favor.";
    const char* opalEventProbe =
        "Everyone is gossiping about what happened a few hours ago. The young "
        "woman Eonislara was found out in the forest, making love with Korol, "
        "a notorious poet and troublemaking warrior of the Heran clan. Korol "
        "left without a formal farewell. Eonislara is unmarried, so her actions "
        "are not an offense against the gods. But she is betrothed to Jorator, "
        "a young thane of the Blackrock clan, and the wedding is scheduled for "
        "next season.";
    const char* clanRelationshipProbe =
        "They're known for keeping close ties with their former kin in "
        "Heortland. Their chief is Hendrick. They're in the Colymar tribe. "
        "They owe us a favor.";
    const char* pigCompositeResult =
        TranslateRuntimeText(pigCompositeProbe);
    const std::string pigCompositeTranslation(
        pigCompositeResult ? pigCompositeResult : "");
    const char* farmerCompositeResult =
        TranslateRuntimeText(farmerCompositeProbe);
    const char* genericCompositeResult =
        TranslateRuntimeText(genericCompositeProbe);
    const char* textOnlyCompositeResult =
        TranslateRuntimeText(textOnlyCompositeProbe);
    const std::string clanSuffixResult(
        TranslateRuntimeText(clanSuffixProbe));
    const std::string clanDescriptionResult(
        TranslateRuntimeText(clanDescriptionProbe));
    const std::string opalEventResult(
        TranslateRuntimeText(opalEventProbe));
    const std::string clanRelationshipResult(
        TranslateRuntimeText(clanRelationshipProbe));
    WriteLog(
        pigCompositeResult != pigCompositeProbe &&
                !pigCompositeTranslation.empty() &&
                farmerCompositeResult != farmerCompositeProbe &&
                genericCompositeResult != genericCompositeProbe &&
                textOnlyCompositeResult != textOnlyCompositeProbe &&
                clanSuffixResult.find("Bachad") == std::string::npos &&
                clanDescriptionResult.find("known for") == std::string::npos &&
                clanDescriptionResult.find("Fantarl") == std::string::npos &&
                opalEventResult.find("Everyone is gossiping") == std::string::npos &&
                opalEventResult.find("Heran") == std::string::npos &&
                opalEventResult.find("Blackrock") == std::string::npos &&
                clanRelationshipResult.find("known for") == std::string::npos &&
                clanRelationshipResult.find("Hendrick") == std::string::npos &&
                clanRelationshipResult.find("Colymar") == std::string::npos
            ? L"runtime composite translation self-test passed"
            : L"runtime composite translation self-test failed");
    wchar_t message[256] = {};
    _snwprintf_s(
        message,
        _countof(message),
        _TRUNCATE,
        L"loaded %u exact translations, %u format patterns, %u Opal runtime templates, and %u clan description aliases",
        static_cast<unsigned>(g_translations.size()),
        static_cast<unsigned>(g_formatPatterns.size()),
        static_cast<unsigned>([]() {
            size_t count = 0;
            for (const auto& bucket : g_opalFormatPatternsByFirstByte) {
                count += bucket.size();
            }
            return count;
        }()),
        static_cast<unsigned>(knownForAliases.size()));
    WriteLog(message);
    return !g_translations.empty();
}

bool MatchFormat(
    const FormatPattern& pattern,
    const std::string& text,
    std::vector<std::string>* captures) {
    if (pattern.literals.empty() ||
        text.compare(0, pattern.literals[0].size(), pattern.literals[0]) != 0) {
        return false;
    }
    captures->clear();
    size_t cursor = pattern.literals[0].size();
    for (size_t index = 0; index < pattern.tokens.size(); ++index) {
        const std::string& nextLiteral = pattern.literals[index + 1];
        size_t end = nextLiteral.empty()
                         ? text.size()
                         : text.find(nextLiteral, cursor);
        if (end == std::string::npos) {
            return false;
        }
        captures->push_back(text.substr(cursor, end - cursor));
        cursor = end + nextLiteral.size();
    }
    return cursor == text.size();
}

std::string RenderFormat(
    const std::string& translated,
    const std::vector<std::string>& captures) {
    std::string result;
    size_t cursor = 0;
    size_t capture = 0;
    while (cursor < translated.size()) {
        if (translated[cursor] != '%' || cursor + 1 >= translated.size()) {
            result.push_back(translated[cursor++]);
            continue;
        }
        size_t end = cursor + 1;
        while (end < translated.size() && end - cursor <= 32) {
            if (IsPrintfConversion(translated[end])) {
                if (translated[end] == '%') {
                    result.push_back('%');
                } else if (capture < captures.size()) {
                    result += captures[capture++];
                }
                cursor = end + 1;
                break;
            }
            ++end;
        }
        if (end >= translated.size() || end - cursor > 32) {
            result.push_back(translated[cursor++]);
        }
    }
    return capture == captures.size() ? result : std::string();
}

bool TranslateKnownComposite(
    const std::string& source, std::string* translated) {
    translated->clear();
    size_t cursor = 0;
    size_t translatedFragments = 0;
    size_t numericCaptures = 0;
    while (cursor < source.size()) {
        const unsigned char first =
            static_cast<unsigned char>(source[cursor]);
        const TranslationEntry* matched = nullptr;
        for (const TranslationEntry* candidate :
             g_compositeFragmentsByFirstByte[first]) {
            if (candidate->first.size() <= source.size() - cursor &&
                source.compare(
                    cursor,
                    candidate->first.size(),
                    candidate->first) == 0) {
                matched = candidate;
                break;
            }
        }
        if (matched) {
            *translated += matched->second;
            cursor += matched->first.size();
            ++translatedFragments;
            continue;
        }
        if (std::isspace(
                static_cast<unsigned char>(source[cursor]))) {
            translated->push_back(source[cursor++]);
            continue;
        }
        const size_t numberStart = cursor;
        size_t numberCursor = cursor;
        if (source[numberCursor] == '+' ||
            source[numberCursor] == '-') {
            ++numberCursor;
        }
        const size_t digitStart = numberCursor;
        while (numberCursor < source.size() &&
               (std::isdigit(static_cast<unsigned char>(
                    source[numberCursor])) ||
                source[numberCursor] == ',')) {
            ++numberCursor;
        }
        if (numberCursor != digitStart) {
            translated->append(
                source,
                numberStart,
                numberCursor - numberStart);
            cursor = numberCursor;
            ++numericCaptures;
            continue;
        }
        if (static_cast<unsigned char>(source[cursor]) < 0x80 &&
            std::ispunct(
                static_cast<unsigned char>(source[cursor]))) {
            translated->push_back(source[cursor++]);
            continue;
        }
        translated->clear();
        return false;
    }
    if (translatedFragments < 2 &&
        !(translatedFragments >= 1 && numericCaptures >= 1)) {
        translated->clear();
        return false;
    }
    return true;
}

const char* TranslateRuntimeText(const char* source) {
    if (!source || !*source) {
        return source;
    }
    const auto exact = g_translations.find(source);
    if (exact != g_translations.end()) {
        return exact->second.c_str();
    }
    const std::string text(source);
    const std::string nearbySuffix = " N";
    const std::string nearbySmallCapitalSuffix = " \xC9\xB4";
    const std::string* matchedNearbySuffix = nullptr;
    if (text.size() > nearbySuffix.size() &&
        text.compare(
            text.size() - nearbySuffix.size(),
            nearbySuffix.size(),
            nearbySuffix) == 0) {
        matchedNearbySuffix = &nearbySuffix;
    } else if (
        text.size() > nearbySmallCapitalSuffix.size() &&
        text.compare(
            text.size() - nearbySmallCapitalSuffix.size(),
            nearbySmallCapitalSuffix.size(),
            nearbySmallCapitalSuffix) == 0) {
        matchedNearbySuffix = &nearbySmallCapitalSuffix;
    }
    if (matchedNearbySuffix) {
        const auto clan = g_translations.find(
            text.substr(0, text.size() - matchedNearbySuffix->size()));
        if (clan != g_translations.end()) {
            thread_local std::string nearbyClan;
            nearbyClan = clan->second + u8"（邻近）";
            return nearbyClan.c_str();
        }
    }
    const std::string knownForPrefix = "They're known for ";
    const std::string chiefSeparator = ". Their chief is ";
    if (text.compare(0, knownForPrefix.size(), knownForPrefix) == 0) {
        const size_t chiefStart = text.find(
            chiefSeparator, knownForPrefix.size());
        if (chiefStart != std::string::npos) {
            const size_t chiefValueStart =
                chiefStart + chiefSeparator.size();
            const size_t chiefEnd = text.find('.', chiefValueStart);
            if (chiefEnd != std::string::npos) {
                const std::string description = text.substr(
                    knownForPrefix.size(),
                    chiefStart - knownForPrefix.size());
                const std::string chief = text.substr(
                    chiefValueStart, chiefEnd - chiefValueStart);
                const std::string tail = chiefEnd + 1 < text.size()
                    ? text.substr(chiefEnd + 1)
                    : std::string();
                const char* translatedDescription =
                    TranslateRuntimeText(description.c_str());
                const std::string chineseDescription(
                    translatedDescription
                        ? translatedDescription
                        : description.c_str());
                const char* translatedChief =
                    TranslateRuntimeText(chief.c_str());
                const std::string chineseChief(
                    translatedChief ? translatedChief : chief.c_str());
                const char* translatedTail = tail.empty()
                    ? ""
                    : TranslateRuntimeText(tail.c_str());
                const std::string chineseTail(
                    translatedTail ? translatedTail : tail.c_str());
                if (chineseDescription != description &&
                    chineseChief != chief &&
                    (tail.empty() || chineseTail != tail)) {
                    thread_local std::string clanSummary;
                    clanSummary = u8"他们因" + chineseDescription;
                    const std::string knownSuffix = u8"闻名";
                    if (clanSummary.size() < knownSuffix.size() ||
                        clanSummary.compare(
                            clanSummary.size() - knownSuffix.size(),
                            knownSuffix.size(),
                            knownSuffix) != 0) {
                        clanSummary += u8"而闻名";
                    }
                    clanSummary += u8"。他们的酋长是";
                    clanSummary += chineseChief;
                    clanSummary += u8"。";
                    clanSummary += chineseTail;
                    return clanSummary.c_str();
                }
            }
        }
    }

    std::string normalizedSource;
    normalizedSource.reserve(std::strlen(source));
    bool pendingSpace = false;
    for (const unsigned char* cursor =
             reinterpret_cast<const unsigned char*>(source);
         *cursor;
         ++cursor) {
        const unsigned char ch = *cursor;
        if (std::isspace(ch)) {
            pendingSpace = !normalizedSource.empty();
            continue;
        }
        if (pendingSpace) {
            normalizedSource.push_back(' ');
            pendingSpace = false;
        }
        normalizedSource.push_back(static_cast<char>(ch));
    }
    const auto normalized =
        g_normalizedTranslations.find(normalizedSource);
    if (normalized != g_normalizedTranslations.end()) {
        return normalized->second.c_str();
    }

    if (normalizedSource.size() > 1 &&
        normalizedSource.back() == ':') {
        const std::string stem =
            normalizedSource.substr(0, normalizedSource.size() - 1);
        const auto stemTranslation = g_translations.find(stem);
        if (stemTranslation != g_translations.end()) {
            thread_local std::string decorated;
            decorated = stemTranslation->second;
            decorated += u8"：";
            return decorated.c_str();
        }
    }

    if (std::strchr(source, '\n') ||
        std::strchr(source, '\r')) {
        thread_local std::string composite;
        composite.clear();
        bool translatedAnyLine = false;
        const char* cursor = source;
        while (*cursor) {
            const char* lineEnd = cursor;
            while (*lineEnd &&
                   *lineEnd != '\r' &&
                   *lineEnd != '\n') {
                ++lineEnd;
            }
            std::string line(
                cursor,
                static_cast<size_t>(lineEnd - cursor));
            const auto exactLine = g_translations.find(line);
            std::string normalizedLine;
            normalizedLine.reserve(line.size());
            bool linePendingSpace = false;
            for (unsigned char ch : line) {
                if (std::isspace(ch)) {
                    linePendingSpace = !normalizedLine.empty();
                    continue;
                }
                if (linePendingSpace) {
                    normalizedLine.push_back(' ');
                    linePendingSpace = false;
                }
                normalizedLine.push_back(static_cast<char>(ch));
            }
            const auto normalizedLineTranslation =
                g_normalizedTranslations.find(normalizedLine);
            if (exactLine != g_translations.end()) {
                translatedAnyLine = true;
                composite += exactLine->second;
            } else if (
                normalizedLineTranslation !=
                g_normalizedTranslations.end()) {
                translatedAnyLine = true;
                composite += normalizedLineTranslation->second;
            } else {
                composite += line;
            }
            while (*lineEnd == '\r' || *lineEnd == '\n') {
                composite.push_back(*lineEnd++);
            }
            cursor = lineEnd;
        }
        if (translatedAnyLine) {
            return composite.c_str();
        }
    }

    thread_local std::string formatted;
    std::vector<std::string> captures;
    for (const FormatPattern& pattern : g_formatPatterns) {
        if (!MatchFormat(pattern, text, &captures)) {
            continue;
        }
        formatted = RenderFormat(pattern.translated, captures);
        if (!formatted.empty()) {
            return formatted.c_str();
        }
    }
    const auto& opalPatterns =
        g_opalFormatPatternsByFirstByte[
            static_cast<unsigned char>(text.front())];
    for (const FormatPattern& pattern : opalPatterns) {
        if (!MatchFormat(pattern, text, &captures)) {
            continue;
        }
        std::vector<std::string> translatedCaptures;
        translatedCaptures.reserve(captures.size());
        bool allCapturesCovered = true;
        for (const std::string& capture : captures) {
            const char* translatedCapture =
                TranslateRuntimeText(capture.c_str());
            const std::string value(
                translatedCapture ? translatedCapture : capture.c_str());
            if (value == capture &&
                std::any_of(
                    capture.begin(),
                    capture.end(),
                    [](unsigned char ch) {
                        return ch < 0x80 && std::isalpha(ch);
                    })) {
                allCapturesCovered = false;
                break;
            }
            translatedCaptures.push_back(value);
        }
        if (!allCapturesCovered) {
            continue;
        }
        formatted = RenderFormat(
            pattern.translated, translatedCaptures);
        if (!formatted.empty()) {
            return formatted.c_str();
        }
    }
    if (TranslateKnownComposite(text, &formatted)) {
        return formatted.c_str();
    }
    return source;
}

std::wstring Utf8ToWide(const char* text) {
    if (!text || !*text) {
        return std::wstring();
    }
    const int needed =
        MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (needed <= 1) {
        return std::wstring();
    }
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, text, -1, wide.data(), needed);
    wide.resize(static_cast<size_t>(needed - 1));
    return wide;
}

BOOL CALLBACK FindGameWindowCallback(HWND window, LPARAM parameter) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId() ||
        !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }
    wchar_t className[80] = {};
    GetClassNameW(window, className, _countof(className));
    if (std::wcscmp(className, kOverlayClassName) == 0) {
        return TRUE;
    }
    RECT client = {};
    if (!GetClientRect(window, &client) ||
        client.right - client.left < 200 ||
        client.bottom - client.top < 150) {
        return TRUE;
    }
    *reinterpret_cast<HWND*>(parameter) = window;
    return FALSE;
}

HWND FindGameWindow() {
    HWND window = nullptr;
    EnumWindows(&FindGameWindowCallback, reinterpret_cast<LPARAM>(&window));
    return window;
}

void DrawOutlinedText(
    HDC dc,
    const std::wstring& text,
    RECT rectangle,
    UINT format) {
    SetTextColor(dc, RGB(0, 0, 0));
    static const POINT offsets[] = {
        {-2, 0}, {2, 0}, {0, -2}, {0, 2},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
    };
    for (const POINT& offset : offsets) {
        RECT outline = rectangle;
        OffsetRect(&outline, offset.x, offset.y);
        DrawTextW(
            dc,
            text.c_str(),
            static_cast<int>(text.size()),
            &outline,
            format);
    }
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(
        dc,
        text.c_str(),
        static_cast<int>(text.size()),
        &rectangle,
        format);
}

LRESULT CALLBACK OverlayWindowProcedure(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(window, &paint);
        RECT client = {};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kOverlayKeyColor);
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);

        std::vector<OverlayCaption> captions;
        AcquireSRWLockShared(&g_overlayLock);
        captions = g_overlayCaptions;
        ReleaseSRWLockShared(&g_overlayLock);

        for (const OverlayCaption& caption : captions) {
            const int savedDc = SaveDC(dc);
            IntersectClipRect(
                dc,
                caption.clipLeft,
                caption.clipTop,
                caption.clipRight,
                caption.clipBottom);
            const int fontHeight =
                std::clamp(caption.fontHeight, 12, 48);
            HFONT font = CreateFontW(
                -fontHeight,
                0,
                0,
                0,
                FW_SEMIBOLD,
                FALSE,
                FALSE,
                FALSE,
                GB2312_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                NONANTIALIASED_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE,
                L"Microsoft YaHei");
            HGDIOBJ oldFont = SelectObject(dc, font);
            RECT rectangle = {
                caption.x,
                caption.y,
                caption.x + caption.width,
                caption.y + caption.height,
            };
            const UINT format =
                DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_NOCLIP |
                (caption.text.size() <= 8
                     ? DT_SINGLELINE
                     : DT_WORDBREAK);
            DrawOutlinedText(dc, caption.text, rectangle, format);
            SelectObject(dc, oldFont);
            DeleteObject(font);
            RestoreDC(dc, savedDc);
        }
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

DWORD WINAPI RunCaptionOverlay(void*) {
    for (int attempt = 0; attempt < 400 && !g_gameWindow; ++attempt) {
        g_gameWindow = FindGameWindow();
        if (!g_gameWindow) {
            Sleep(25);
        }
    }
    if (!g_gameWindow) {
        WriteLog(L"caption overlay could not find the game window");
        return 1;
    }

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance =
        reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    windowClass.lpfnWndProc = &OverlayWindowProcedure;
    windowClass.lpszClassName = kOverlayClassName;
    windowClass.hCursor =
        LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassExW(&windowClass);

    g_overlayWindow = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT |
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClassName,
        L"",
        WS_POPUP,
        0,
        0,
        1,
        1,
        g_gameWindow,
        nullptr,
        windowClass.hInstance,
        nullptr);
    if (!g_overlayWindow) {
        WriteLog(L"failed to create the caption overlay window");
        return 1;
    }
    SetLayeredWindowAttributes(
        g_overlayWindow, kOverlayKeyColor, 255, LWA_COLORKEY);
    WriteLog(L"caption overlay window ready");

    MSG message = {};
    while (IsWindow(g_gameWindow)) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                return 0;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        RECT client = {};
        POINT origin = {};
        const bool shouldShow =
            IsWindowVisible(g_gameWindow) &&
            !IsIconic(g_gameWindow) &&
            GetClientRect(g_gameWindow, &client) &&
            ClientToScreen(g_gameWindow, &origin);
        if (shouldShow) {
            const RECT target = {
                origin.x,
                origin.y,
                origin.x + (client.right - client.left),
                origin.y + (client.bottom - client.top),
            };
            // 位置/大小/可见性未变化时不再调用 SetWindowPos，
            // 避免 color-key layered 窗口被反复重定位导致闪烁。
            const bool rectChanged =
                !g_overlayWindowVisible ||
                g_overlayWindowRect.left != target.left ||
                g_overlayWindowRect.top != target.top ||
                g_overlayWindowRect.right != target.right ||
                g_overlayWindowRect.bottom != target.bottom;
            if (rectChanged) {
                SetWindowPos(
                    g_overlayWindow,
                    HWND_TOPMOST,
                    target.left,
                    target.top,
                    target.right - target.left,
                    target.bottom - target.top,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
                g_overlayWindowRect = target;
                g_overlayWindowVisible = TRUE;
                InterlockedExchange(&g_overlayDirty, 1);
            }
        } else if (g_overlayWindowVisible) {
            ShowWindow(g_overlayWindow, SW_HIDE);
            g_overlayWindowVisible = FALSE;
            g_overlayWindowRect = {};
        }
        // 过期 caption 清理移到主循环：停止无条件重绘后，
        // 若仍放在 WM_PAINT 里，过期文字将残留无法清除。
        const DWORD now = GetTickCount();
        AcquireSRWLockExclusive(&g_overlayLock);
        const size_t before = g_overlayCaptions.size();
        g_overlayCaptions.erase(
            std::remove_if(
                g_overlayCaptions.begin(),
                g_overlayCaptions.end(),
                [now](const OverlayCaption& caption) {
                    return now - caption.updatedAt > 750;
                }),
            g_overlayCaptions.end());
        const bool expiredChanged =
            g_overlayCaptions.size() != before;
        ReleaseSRWLockExclusive(&g_overlayLock);
        if (expiredChanged) {
            InterlockedExchange(&g_overlayDirty, 1);
        }
        // 仅在 caption 数据或窗口状态真正变化时重绘，静态界面完全静止。
        if (InterlockedExchange(&g_overlayDirty, 0)) {
            InvalidateRect(g_overlayWindow, nullptr, FALSE);
        }
        Sleep(16);
    }
    DestroyWindow(g_overlayWindow);
    g_overlayWindow = nullptr;
    return 0;
}

bool AddDrawableOwnerPosition(
    void* drawable, int* x, int* y) {
    if (!drawable || !x || !y || !g_runtimeImageBase) {
        return false;
    }
    const uintptr_t labelVtable =
        reinterpret_cast<uintptr_t>(
            g_runtimeImageBase + 0x349E4C);
    const uintptr_t buttonVtable =
        reinterpret_cast<uintptr_t>(
            g_runtimeImageBase + 0x423134);
    const LONG count = (std::min)(
        InterlockedCompareExchange(&g_uiElementCount, 0, 0),
        static_cast<LONG>(_countof(g_uiElements)));
    unsigned char* owner = nullptr;
    for (LONG index = 0; index < count && !owner; ++index) {
        auto* candidate =
            static_cast<unsigned char*>(g_uiElements[index]);
        if (!candidate) {
            continue;
        }
        const uintptr_t vtable =
            *reinterpret_cast<const uintptr_t*>(candidate);
        size_t objectBytes = 0;
        if (vtable == buttonVtable) {
            objectBytes = 0xF8;
        } else if (vtable == labelVtable) {
            objectBytes = 0xE4;
        }
        for (size_t offset = sizeof(void*);
             objectBytes != 0 &&
             offset + sizeof(void*) <= objectBytes;
             offset += sizeof(void*)) {
            if (*reinterpret_cast<void**>(candidate + offset) ==
                drawable) {
                owner = candidate;
                static LONG ownerLogCount = 0;
                if (InterlockedIncrement(&ownerLogCount) <= 12) {
                    wchar_t detail[180] = {};
                    _snwprintf_s(
                        detail,
                        _countof(detail),
                        _TRUNCATE,
                        L"matched caption drawable to UI owner at +0x%02X",
                        static_cast<unsigned>(offset));
                    WriteLog(detail);
                }
                break;
            }
        }
    }
    if (!owner) {
        return false;
    }

    unsigned char* current = owner;
    for (int depth = 0; current && depth < 64; ++depth) {
        MEMORY_BASIC_INFORMATION memory = {};
        if (VirtualQuery(current, &memory, sizeof(memory)) !=
                sizeof(memory) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            break;
        }
        *x += *reinterpret_cast<int*>(current + 92);
        *y += *reinterpret_cast<int*>(current + 96);
        current = *reinterpret_cast<unsigned char**>(current + 24);
    }
    return true;
}

bool QueueTranslatedCaption(
    void* drawable, const char* source, int drawContext) {
    if (!drawable || !source || !*source || !g_runtimeImageBase) {
        return false;
    }
    const char* translated = TranslateRuntimeText(source);
    const bool alreadyTranslated =
        g_translatedValues.find(source) != g_translatedValues.end();
    if (translated == source && !alreadyTranslated) {
        return false;
    }
    const char* visibleText = translated != source ? translated : source;
    std::wstring wide = Utf8ToWide(visibleText);
    if (wide.empty()) {
        return false;
    }

    auto* bytes = static_cast<unsigned char*>(drawable);
    int bounds[4] = {};
    using GetPreparedBoundsType = int*(__cdecl*)(int*, int);
    auto getPreparedBounds =
        reinterpret_cast<GetPreparedBoundsType>(
            g_runtimeImageBase + 0xA8279);
    getPreparedBounds(
        bounds,
        static_cast<int>(
            reinterpret_cast<uintptr_t>(bytes + 36)));

    const int originX = *reinterpret_cast<int*>(bytes + 24);
    const int originY = *reinterpret_cast<int*>(bytes + 28);
    int fontHeight = 18;
    void* font = *reinterpret_cast<void**>(bytes + 8);
    if (font) {
        fontHeight = static_cast<int>(
            *reinterpret_cast<unsigned short*>(
                static_cast<unsigned char*>(font) + 18));
    }
    if (fontHeight < 8 || fontHeight > 96) {
        fontHeight = bounds[3] > 0 ? bounds[3] : 18;
    }

    const int localLeft = originX + bounds[0];
    const int localTop = originY + bounds[1];
    const int localRight = localLeft + bounds[2];
    const int localBottom = localTop + bounds[3];
    int captionX = localLeft;
    int captionY = localTop;
    int captionRight = localRight;
    int captionBottom = localBottom;
    int clientWidth = 512;
    int clientHeight = 384;
    RECT gameClient = {};
    if (g_gameWindow &&
        GetClientRect(g_gameWindow, &gameClient)) {
        clientWidth =
            (std::max)(gameClient.right - gameClient.left, 1L);
        clientHeight =
            (std::max)(gameClient.bottom - gameClient.top, 1L);
    }
    int clipLeft = 0;
    int clipTop = 0;
    int clipRight = clientWidth;
    int clipBottom = clientHeight;
    if (drawContext) {
        const auto* graphics =
            reinterpret_cast<const unsigned char*>(drawContext);
        const int m00 = *reinterpret_cast<const int*>(graphics + 8);
        const int m01 = *reinterpret_cast<const int*>(graphics + 12);
        const int m10 = *reinterpret_cast<const int*>(graphics + 16);
        const int m11 = *reinterpret_cast<const int*>(graphics + 20);
        const int tx = *reinterpret_cast<const int*>(graphics + 24);
        const int ty = *reinterpret_cast<const int*>(graphics + 28);
        auto transformPoint =
            [m00, m01, m10, m11, tx, ty,
             clientWidth, clientHeight](
                int x, int y, int* outX, int* outY) {
                const long long fixedX =
                    static_cast<long long>(x) << 3;
                const long long fixedY =
                    static_cast<long long>(y) << 3;
                const long long transformedX =
                    ((static_cast<long long>(m00) * fixedX +
                      static_cast<long long>(m10) * fixedY) >>
                     12) +
                    tx;
                const long long transformedY =
                    ((static_cast<long long>(m01) * fixedX +
                      static_cast<long long>(m11) * fixedY) >>
                     12) +
                    ty;
                *outX = static_cast<int>(
                    transformedX * clientWidth / (512 * 16));
                *outY = static_cast<int>(
                    transformedY * clientHeight / (384 * 16));
            };
        int cornersX[4] = {};
        int cornersY[4] = {};
        transformPoint(
            localLeft, localTop, &cornersX[0], &cornersY[0]);
        transformPoint(
            localRight, localTop, &cornersX[1], &cornersY[1]);
        transformPoint(
            localLeft, localBottom, &cornersX[2], &cornersY[2]);
        transformPoint(
            localRight,
            localBottom,
            &cornersX[3],
            &cornersY[3]);
        captionX = *std::min_element(cornersX, cornersX + 4);
        captionY = *std::min_element(cornersY, cornersY + 4);
        captionRight = *std::max_element(cornersX, cornersX + 4);
        captionBottom = *std::max_element(cornersY, cornersY + 4);

        void* frame =
            *reinterpret_cast<void* const*>(graphics + 4);
        if (frame) {
            int frameBounds[4] = {};
            using GetRegionBoundsType =
                int*(__thiscall*)(void*, int*);
            auto getRegionBounds =
                reinterpret_cast<GetRegionBoundsType>(
                    g_runtimeImageBase + 0xA7B59);
            getRegionBounds(frame, frameBounds);
            clipLeft = static_cast<int>(
                static_cast<long long>(frameBounds[0]) *
                clientWidth / (512 * 16));
            clipTop = static_cast<int>(
                static_cast<long long>(frameBounds[1]) *
                clientHeight / (384 * 16));
            clipRight = clipLeft + static_cast<int>(
                static_cast<long long>(frameBounds[2]) *
                clientWidth / (512 * 16));
            clipBottom = clipTop + static_cast<int>(
                static_cast<long long>(frameBounds[3]) *
                clientHeight / (384 * 16));
        }
    }
    const int measuredWidth =
        captionRight - captionX;
    const int measuredHeight =
        captionBottom - captionY;
    const bool isMainMenuCaption =
        std::strcmp(source, "Play") == 0 ||
        std::strcmp(source, "Settings") == 0 ||
        std::strcmp(source, "Info") == 0 ||
        std::strcmp(visibleText, u8"游玩") == 0 ||
        std::strcmp(visibleText, u8"设置") == 0 ||
        std::strcmp(visibleText, u8"信息") == 0;
    const int scaledFontHeight = (std::max)(
        isMainMenuCaption
            ? fontHeight * clientHeight / 384
            : fontHeight,
        12);
    const int desiredWidth =
        isMainMenuCaption
            ? scaledFontHeight *
                  static_cast<int>(wide.size()) +
                  scaledFontHeight / 2
            : measuredWidth;
    const int finalWidth =
        (std::max)(measuredWidth, desiredWidth);
    captionX -= (finalWidth - measuredWidth) / 2;
    OverlayCaption caption = {
        std::move(wide),
        captionX,
        captionY,
        finalWidth,
        (std::max)(
            measuredHeight,
            isMainMenuCaption
                ? scaledFontHeight + scaledFontHeight / 2
                : scaledFontHeight),
        scaledFontHeight,
        clipLeft,
        clipTop,
        clipRight,
        clipBottom,
        GetTickCount(),
    };
    if (caption.x + caption.width <= caption.clipLeft ||
        caption.x >= caption.clipRight ||
        caption.y + caption.height <= caption.clipTop ||
        caption.y >= caption.clipBottom) {
        return true;
    }
    static LONG positionLogCount = 0;
    if (InterlockedIncrement(&positionLogCount) <= 20) {
        wchar_t detail[300] = {};
        _snwprintf_s(
            detail,
            _countof(detail),
            _TRUNCATE,
            L"overlay caption '%s': x=%d y=%d w=%d h=%d transformed=%d",
            caption.text.c_str(),
            caption.x,
            caption.y,
            caption.width,
            caption.height,
            drawContext ? 1 : 0);
        WriteLog(detail);
    }
    AcquireSRWLockExclusive(&g_overlayLock);
    auto existing = std::find_if(
        g_overlayCaptions.begin(),
        g_overlayCaptions.end(),
        [&caption](const OverlayCaption& candidate) {
            return candidate.text == caption.text &&
                   std::abs(candidate.x - caption.x) <= 2 &&
                   std::abs(candidate.y - caption.y) <= 2;
        });
    bool changed = false;
    if (existing != g_overlayCaptions.end()) {
        changed =
            existing->x != caption.x ||
            existing->y != caption.y ||
            existing->width != caption.width ||
            existing->height != caption.height ||
            existing->fontHeight != caption.fontHeight ||
            existing->clipLeft != caption.clipLeft ||
            existing->clipTop != caption.clipTop ||
            existing->clipRight != caption.clipRight ||
            existing->clipBottom != caption.clipBottom;
        if (changed) {
            *existing = caption;
        }
    } else {
        g_overlayCaptions.push_back(std::move(caption));
        changed = true;
    }
    ReleaseSRWLockExclusive(&g_overlayLock);
    if (changed) {
        InterlockedExchange(&g_overlayDirty, 1);
    }
    return true;
}

void LogTranslationHit(const char* source, const char* translated) {
    if (source == translated ||
        InterlockedIncrement(&g_translationLogCount) > 20) {
        return;
    }
    const int needed =
        MultiByteToWideChar(CP_UTF8, 0, translated, -1, nullptr, 0);
    if (needed <= 1) {
        return;
    }
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, translated, -1, wide.data(), needed);
    wide.resize(static_cast<size_t>(needed - 1));
    const std::wstring message = L"translated runtime text: " + wide.substr(0, 160);
    WriteLog(message.c_str());
}

bool LoadExternalFontData() {
    if (!g_externalFontData.empty()) {
        return true;
    }
    wchar_t windowsDirectory[MAX_PATH] = {};
    if (!GetWindowsDirectoryW(
            windowsDirectory, _countof(windowsDirectory))) {
        return false;
    }
    std::wstring fontPath = windowsDirectory;
    fontPath += L"\\Fonts\\simhei.ttf";
    HANDLE file = g_originalCreateFileW(
        fontPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER fileSize = {};
    const bool validSize =
        GetFileSizeEx(file, &fileSize) != FALSE &&
        fileSize.QuadPart > 0 &&
        fileSize.QuadPart <= 32 * 1024 * 1024;
    if (!validSize) {
        CloseHandle(file);
        return false;
    }
    g_externalFontData.resize(
        static_cast<size_t>(fileSize.QuadPart));
    DWORD bytesRead = 0;
    const bool loaded = g_originalReadFile(
                            file,
                            g_externalFontData.data(),
                            static_cast<DWORD>(
                                g_externalFontData.size()),
                            &bytesRead,
                            nullptr) != FALSE &&
                        bytesRead == g_externalFontData.size();
    CloseHandle(file);
    if (!loaded) {
        g_externalFontData.clear();
    }
    return loaded;
}

void __stdcall PrepareExternalFont(void* font) {
    if (!font || !g_runtimeImageBase ||
        g_externalFontReplacements.find(font) !=
            g_externalFontReplacements.end()) {
        return;
    }

    if (!LoadExternalFontData()) {
        if (InterlockedCompareExchange(&g_fontOverrideLogCount, 1, 0) == 0) {
            WriteLog(L"the Windows SimHei TrueType font was not found");
        }
        return;
    }

    auto* sourceFontBytes = static_cast<unsigned char*>(font);
    int sourcePointSize = *reinterpret_cast<int*>(sourceFontBytes + 52);
    if (sourcePointSize <= 0 || sourcePointSize > 96) {
        sourcePointSize = static_cast<int>(
            *reinterpret_cast<unsigned short*>(sourceFontBytes + 18));
        if (sourcePointSize < 8 || sourcePointSize > 64) {
            sourcePointSize = 18;
        }
    }
    const int pointSize = (std::max)(5, (sourcePointSize + 1) / 3);
    const auto sharedFont =
        g_externalFontsByPointSize.find(pointSize);
    if (sharedFont != g_externalFontsByPointSize.end()) {
        g_externalFontReplacements.emplace(font, sharedFont->second);
        return;
    }

    using CreateTTDescriptorType = void*(__cdecl*)();
    using InitialiseFontFieldsType = int(__thiscall*)(void*);
    using InitialiseFontContainerType = void(__thiscall*)(void*, int);
    using GameAllocateType = void*(__cdecl*)(size_t);
    auto createDescriptor = reinterpret_cast<CreateTTDescriptorType>(
        g_runtimeImageBase + 0x7089C);
    auto initialiseFontFields =
        reinterpret_cast<InitialiseFontFieldsType>(
            g_runtimeImageBase + 0x6C407);
    auto initialiseContainer32 =
        reinterpret_cast<InitialiseFontContainerType>(
            g_runtimeImageBase + 0x6CE6A);
    auto initialiseContainer68 =
        reinterpret_cast<InitialiseFontContainerType>(
            g_runtimeImageBase + 0x6CBA0);
    auto initialiseContainer96 =
        reinterpret_cast<InitialiseFontContainerType>(
            g_runtimeImageBase + 0x6CBCA);
    auto gameAllocate = reinterpret_cast<GameAllocateType>(
        g_runtimeImageBase + 0xB77E0);
    void* replacementFontMemory = gameAllocate(120);
    void* replacementFont = replacementFontMemory;
    if (replacementFont) {
        std::memset(replacementFont, 0, 120);
        std::memcpy(replacementFont, sourceFontBytes, 16);
        initialiseContainer32(
            static_cast<unsigned char*>(replacementFont) + 32, 0);
        initialiseContainer68(
            static_cast<unsigned char*>(replacementFont) + 68, 0);
        initialiseContainer96(
            static_cast<unsigned char*>(replacementFont) + 96, 0);
        initialiseFontFields(replacementFont);
    }
    auto* descriptor =
        static_cast<unsigned char*>(createDescriptor());
    if (!replacementFont || !descriptor) {
        WriteLog(L"failed to allocate external TrueType font descriptor");
        return;
    }
    *reinterpret_cast<char**>(descriptor + 16) = nullptr;
    *reinterpret_cast<void**>(descriptor + 20) =
        g_externalFontData.data();
    *reinterpret_cast<unsigned int*>(descriptor + 24) =
        static_cast<unsigned int>(g_externalFontData.size());
    auto* replacementBytes =
        static_cast<unsigned char*>(replacementFont);
    replacementBytes[16] = sourceFontBytes[16];
    replacementBytes[17] = sourceFontBytes[17];
    *reinterpret_cast<int*>(replacementBytes + 24) =
        *reinterpret_cast<int*>(sourceFontBytes + 24);
    *reinterpret_cast<int*>(replacementBytes + 52) = pointSize;
    *reinterpret_cast<int*>(replacementBytes + 56) = 0;
    *reinterpret_cast<void**>(replacementBytes + 48) = descriptor;
    using GetFontRendererType = void*(__cdecl*)();
    using CreateFontRendererType =
        void*(__thiscall*)(void*, const void*, int, int);
    using FinaliseFontType = int(__thiscall*)(void*);
    auto getFontRenderer = reinterpret_cast<GetFontRendererType>(
        g_runtimeImageBase + 0x6D610);
    auto createFontRenderer =
        reinterpret_cast<CreateFontRendererType>(
            g_runtimeImageBase + 0x70DB7);
    auto finaliseFont = reinterpret_cast<FinaliseFontType>(
        g_runtimeImageBase + 0x6CC6D);
    void* rendererInterface = getFontRenderer();
    void* renderer = rendererInterface
        ? createFontRenderer(
              rendererInterface,
              g_externalFontData.data(),
              static_cast<int>(g_externalFontData.size()),
              pointSize)
        : nullptr;
    *reinterpret_cast<void**>(replacementBytes + 60) = renderer;
    if (renderer) {
        finaliseFont(replacementFont);
    }
    if (!*reinterpret_cast<void**>(replacementBytes + 60)) {
        WriteLog(L"external TrueType font renderer creation failed");
        return;
    }
    g_externalFontsByPointSize.emplace(pointSize, replacementFont);
    g_externalFontReplacements.emplace(font, replacementFont);

    const LONG count = InterlockedIncrement(&g_fontOverrideLogCount);
    if (count <= 20) {
        wchar_t message[MAX_PATH + 80] = {};
        _snwprintf_s(
            message,
            _countof(message),
            _TRUNCATE,
            L"created in-memory SimHei font (point size %d)",
            pointSize);
        WriteLog(message);
    }
}

void __cdecl HookFontPrepareString(
    int preparedData, const char* source, unsigned int length) {
    if (!g_originalFontPrepareString || !source || !*source) {
        if (g_originalFontPrepareString) {
            g_originalFontPrepareString(preparedData, source, length);
        }
        return;
    }

    const size_t sourceLength = length == static_cast<unsigned int>(-1)
        ? std::strlen(source)
        : static_cast<size_t>(length);
    const std::string original(source, sourceLength);
    const char* translated = TranslateRuntimeText(original.c_str());
    if (!translated || original == translated) {
        g_originalFontPrepareString(preparedData, source, length);
        return;
    }

    void* originalFont = nullptr;
    void* replacementFont = nullptr;
    if (g_runtimeImageBase) {
        auto* context = *reinterpret_cast<unsigned char**>(
            g_runtimeImageBase + 0x4DBAD8);
        if (context) {
            originalFont = *reinterpret_cast<void**>(context + 16);
            PrepareExternalFont(originalFont);
            const auto replacement =
                g_externalFontReplacements.find(originalFont);
            if (replacement != g_externalFontReplacements.end()) {
                replacementFont = replacement->second;
            }
        }
    }

    using SelectFontType = int(__cdecl*)(void*);
    auto selectFont = g_runtimeImageBase
        ? reinterpret_cast<SelectFontType>(g_runtimeImageBase + 0x946A5)
        : nullptr;
    if (selectFont && replacementFont) {
        selectFont(replacementFont);
    }
    LogTranslationHit(original.c_str(), translated);
    g_originalFontPrepareString(
        preparedData,
        translated,
        static_cast<unsigned int>(std::strlen(translated)));
    // Keep the replacement selected through the subsequent map draw.  The
    // prepared glyphs reference its atlas; restoring the bitmap font here
    // makes the renderer interpret Chinese glyph indices as Latin tiles.
}

bool ReplaceDrawableFont(void* drawable) {
    if (!drawable) {
        return false;
    }
    auto** fontSlot = reinterpret_cast<void**>(
        static_cast<unsigned char*>(drawable) + 8);
    const auto replacement =
        g_externalFontReplacements.find(*fontSlot);
    if (replacement != g_externalFontReplacements.end()) {
        *fontSlot = replacement->second;
        return true;
    }
    for (const auto& entry : g_externalFontReplacements) {
        if (entry.second == *fontSlot) {
            return true;
        }
    }
    return false;
}

bool PrepareNativeTranslatedCaption(
    void* drawable,
    int preparedData,
    int* size,
    char oneLine,
    int* nativeResult = nullptr) {
    if (!drawable || !g_setPropertyString ||
        !g_originalCaptionPrepare) {
        return false;
    }
    auto* bytes = static_cast<unsigned char*>(drawable);
    const auto* holder =
        reinterpret_cast<const void* const*>(bytes + 4);
    const char* source =
        *holder ? *static_cast<const char* const*>(*holder) : "";
    if (!source || !*source) {
        return false;
    }
    const char* translated = TranslateRuntimeText(source);
    const bool hasTranslation =
        translated != source ||
        g_translatedValues.find(source) != g_translatedValues.end();
    if (!hasTranslation) {
        return false;
    }
    static LONG nativePrepareLogs = 0;
    if (InterlockedIncrement(&nativePrepareLogs) <= 12) {
        WriteLog(L"native translated caption prepare requested");
    }

    if (!ReplaceDrawableFont(drawable)) {
        void* sourceFont = *reinterpret_cast<void**>(bytes + 8);
        PrepareExternalFont(sourceFont);
        if (!ReplaceDrawableFont(drawable)) {
            return false;
        }
    }

    const std::string original(source);
    const std::string nativeText(translated);
    LogTranslationHit(original.c_str(), nativeText.c_str());
    g_setPropertyString(bytes + 4, nativeText.c_str());
    const int result = g_originalCaptionPrepare(
        drawable, preparedData, size, oneLine);
    g_setPropertyString(bytes + 4, original.c_str());
    g_nativePreparedCaptions[drawable] = original;
    if (nativeResult) {
        *nativeResult = result;
    }
    return true;
}

int __cdecl HookResolveResource(int* resource, const char* type, int flags) {
    if (resource && *resource && type &&
        std::strcmp(type, "CIwGxFontTTFont") == 0 &&
        g_runtimeImageBase &&
        **reinterpret_cast<uintptr_t**>(resource) ==
            reinterpret_cast<uintptr_t>(g_runtimeImageBase + 0x3CBA90)) {
        return *resource;
    }
    return g_originalResolveResource(resource, type, flags);
}

short __fastcall HookFontSerialise(void* self, void*) {
    const short result = g_originalFontSerialise(self);
    static LONG hitCount = 0;
    if (InterlockedIncrement(&hitCount) <= 20) {
        WriteLog(L"font serialise hit");
    }
    return result;
}

#if defined(_M_IX86)
__declspec(naked) void HookFontFinalizeTail() {
    __asm {
        pushfd
        pushad
        push esi
        call PrepareExternalFont
        popad
        popfd
        jmp dword ptr [g_originalFontFinalizeTail]
    }
}
#endif

int* __fastcall HookAdvisorText(
    void* self, void*, const char* source, int advisorIndex) {
    return g_originalAdvisorText(self, source, advisorIndex);
}

int __fastcall HookCaptionPrepare(
    void* self, void*, int preparedData, int* size, char oneLine) {
    if (InterlockedIncrement(&g_captionPrepareCount) <= 40) {
        WriteLog(L"caption prepare hit");
    }
    int nativeResult = 0;
    if (PrepareNativeTranslatedCaption(
            self, preparedData, size, oneLine, &nativeResult)) {
        return nativeResult;
    }
    return g_originalCaptionPrepare(
        self, preparedData, size, oneLine);
}

int __fastcall HookPrimitiveQueue(
    void* self,
    void*,
    void* primitive,
    int material) {
    const int result =
        g_originalPrimitiveQueue(self, primitive, material);
    if (g_captionVisibilityProbeDepth > 0 && result != 0) {
        g_captionVisibilityAccepted = true;
    }
    return result;
}

int __fastcall HookCaptionDraw(
    void* self, void*, int drawContext) {
    InterlockedIncrement(&g_captionDrawCount);
    auto* bytes = static_cast<unsigned char*>(self);
    const auto* holder =
        reinterpret_cast<const void* const*>(bytes + 4);
    const char* source =
        *holder ? *static_cast<const char* const*>(*holder) : "";
    const char* translated = TranslateRuntimeText(source);
    const bool hasTranslation =
        translated != source ||
        g_translatedValues.find(source) !=
            g_translatedValues.end();
    if (!hasTranslation) {
        return g_originalCaptionDraw(self, drawContext);
    }

    const auto prepared = g_nativePreparedCaptions.find(self);
    if (prepared == g_nativePreparedCaptions.end() ||
        prepared->second != source) {
        int size[2] = {
            *reinterpret_cast<int*>(bytes + 84),
            *reinterpret_cast<int*>(bytes + 88),
        };
        PrepareNativeTranslatedCaption(
            self,
            static_cast<int>(
                reinterpret_cast<uintptr_t>(bytes + 36)),
            size,
            std::strchr(source, '\n') == nullptr &&
                std::strchr(source, '\r') == nullptr);
    }
    return g_originalCaptionDraw(self, drawContext);
}

void RegisterUIElement(void* element) {
    if (!element) {
        return;
    }
    const LONG index = InterlockedIncrement(&g_uiElementCount) - 1;
    if (index >= 0 &&
        index < static_cast<LONG>(_countof(g_uiElements))) {
        g_uiElements[index] = element;
    }
}

void* __cdecl HookLabelFactory() {
    void* element = g_originalLabelFactory();
    RegisterUIElement(element);
    return element;
}

void* __cdecl HookButtonFactory() {
    void* element = g_originalButtonFactory();
    RegisterUIElement(element);
    return element;
}

void ScanLiveUIElements() {
    if (!g_runtimeImageBase) {
        return;
    }
    const uintptr_t labelVtable =
        reinterpret_cast<uintptr_t>(
            g_runtimeImageBase + 0x349E4C);
    const uintptr_t buttonVtable =
        reinterpret_cast<uintptr_t>(
            g_runtimeImageBase + 0x423134);
    InterlockedExchange(&g_uiElementCount, 0);

    SYSTEM_INFO systemInfo = {};
    GetSystemInfo(&systemInfo);
    auto* cursor = static_cast<unsigned char*>(
        systemInfo.lpMinimumApplicationAddress);
    auto* maximum = static_cast<unsigned char*>(
        systemInfo.lpMaximumApplicationAddress);
    while (cursor < maximum) {
        MEMORY_BASIC_INFORMATION memory = {};
        if (VirtualQuery(cursor, &memory, sizeof(memory)) != sizeof(memory)) {
            break;
        }
        const DWORD blocked = PAGE_GUARD | PAGE_NOACCESS;
        const DWORD writable =
            PAGE_READWRITE | PAGE_WRITECOPY |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (memory.State == MEM_COMMIT &&
            memory.Type == MEM_PRIVATE &&
            (memory.Protect & blocked) == 0 &&
            (memory.Protect & writable) != 0 &&
            memory.RegionSize >= sizeof(uintptr_t)) {
            auto* first = static_cast<unsigned char*>(memory.BaseAddress);
            auto* end = first + memory.RegionSize;
            for (auto* address = first;
                 address + sizeof(uintptr_t) <= end;
                 address += sizeof(uintptr_t)) {
                const uintptr_t value =
                    *reinterpret_cast<const uintptr_t*>(address);
                if (value == labelVtable || value == buttonVtable) {
                    RegisterUIElement(address);
                }
            }
        }
        auto* next =
            static_cast<unsigned char*>(memory.BaseAddress) +
            memory.RegionSize;
        if (next <= cursor) {
            break;
        }
        cursor = next;
    }
    static LONG scanLogs = 0;
    if (InterlockedIncrement(&scanLogs) <= 20) {
        wchar_t message[160] = {};
        _snwprintf_s(
            message,
            _countof(message),
            _TRUNCATE,
            L"live UI scan found %u label/button objects",
            static_cast<unsigned>(
                InterlockedCompareExchange(
                    &g_uiElementCount, 0, 0)));
        WriteLog(message);
    }
}

void TranslateRegisteredUICaptions() {
    static DWORD lastRun = 0;
    static DWORD lastFullScan = 0;
    if (!g_runtimeImageBase || !g_originalGetTickCount) {
        return;
    }
    const DWORD now = g_originalGetTickCount();
    if (now - lastRun < 250) {
        return;
    }
    lastRun = now;
    if (now - lastFullScan >= 1000) {
        lastFullScan = now;
        ScanLiveUIElements();
    }

    const uintptr_t labelVtable =
        reinterpret_cast<uintptr_t>(
            g_runtimeImageBase + 0x349E4C);
    const uintptr_t buttonVtable =
        reinterpret_cast<uintptr_t>(
            g_runtimeImageBase + 0x423134);
    const uintptr_t labelVtable1 =
        reinterpret_cast<uintptr_t>(
            g_runtimeImageBase + 0x349F18);
    const uintptr_t labelVtable2 =
        reinterpret_cast<uintptr_t>(
            g_runtimeImageBase + 0x349F30);
    const uintptr_t buttonVtable1 =
        reinterpret_cast<uintptr_t>(
            g_runtimeImageBase + 0x423118);
    const uintptr_t buttonVtable2 =
        reinterpret_cast<uintptr_t>(
            g_runtimeImageBase + 0x423108);
    const uintptr_t drawableTextVtable =
        reinterpret_cast<uintptr_t>(
            g_runtimeImageBase + 0x423350);
    const LONG count = min(
        InterlockedCompareExchange(&g_uiElementCount, 0, 0),
        static_cast<LONG>(_countof(g_uiElements)));
    size_t refreshedCount = 0;
    for (LONG index = 0; index < count; ++index) {
        void* element = g_uiElements[index];
        if (!element) {
            continue;
        }
        MEMORY_BASIC_INFORMATION memory = {};
        if (VirtualQuery(element, &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            continue;
        }
        const uintptr_t vtable =
            *static_cast<const uintptr_t*>(element);
        if (vtable != labelVtable && vtable != buttonVtable) {
            continue;
        }
        const auto* objectBytes =
            static_cast<const unsigned char*>(element);
        const uintptr_t secondary1 =
            *reinterpret_cast<const uintptr_t*>(
                objectBytes + 0x10);
        const uintptr_t secondary2 =
            *reinterpret_cast<const uintptr_t*>(
                objectBytes + 0x14);
        if (vtable == labelVtable &&
            (secondary1 != labelVtable1 ||
             secondary2 != labelVtable2)) {
            continue;
        }
        if (vtable == buttonVtable &&
            (secondary1 != buttonVtable1 ||
             secondary2 != buttonVtable2)) {
            continue;
        }
        static LONG drawablePointerLogs = 0;
        if (InterlockedIncrement(&drawablePointerLogs) <= 24) {
            const size_t inspectOffsets[] =
                {0xDC, 0xE0, 0xE4, 0xE8, 0xEC};
            for (size_t inspectOffset : inspectOffsets) {
                void* pointer =
                    *reinterpret_cast<void**>(
                        const_cast<unsigned char*>(objectBytes) +
                        inspectOffset);
                uintptr_t childVtable = 0;
                MEMORY_BASIC_INFORMATION childMemory = {};
                if (pointer &&
                    VirtualQuery(
                        pointer,
                        &childMemory,
                        sizeof(childMemory)) ==
                        sizeof(childMemory) &&
                    childMemory.State == MEM_COMMIT &&
                    (childMemory.Protect &
                     (PAGE_GUARD | PAGE_NOACCESS)) == 0) {
                    childVtable =
                        *static_cast<const uintptr_t*>(pointer);
                }
                wchar_t detail[220] = {};
                _snwprintf_s(
                    detail,
                    _countof(detail),
                    _TRUNCATE,
                    L"UI object %s +0x%02X -> 0x%08X vtable=0x%08X",
                    vtable == buttonVtable
                        ? L"button"
                        : L"label",
                    static_cast<unsigned>(inspectOffset),
                    static_cast<unsigned>(
                        reinterpret_cast<uintptr_t>(pointer)),
                    static_cast<unsigned>(childVtable));
                WriteLog(detail);
            }
        }
        const size_t drawableOffsets[] =
            {0xDC, 0xE0, 0xEC};
        for (size_t drawableOffset : drawableOffsets) {
            if ((vtable == labelVtable &&
                 drawableOffset == 0xEC) ||
                (vtable == buttonVtable &&
                 drawableOffset != 0xEC)) {
                continue;
            }
            void* drawable =
                *reinterpret_cast<void**>(
                    const_cast<unsigned char*>(objectBytes) +
                    drawableOffset);
            MEMORY_BASIC_INFORMATION drawableMemory = {};
            if (!drawable ||
                VirtualQuery(
                    drawable,
                    &drawableMemory,
                    sizeof(drawableMemory)) != sizeof(drawableMemory) ||
                drawableMemory.State != MEM_COMMIT ||
                (drawableMemory.Protect &
                 (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
                *static_cast<const uintptr_t*>(drawable) !=
                    drawableTextVtable) {
                continue;
            }
            auto* drawableBytes =
                static_cast<unsigned char*>(drawable);
            const auto* holder =
                reinterpret_cast<const void* const*>(
                    drawableBytes + 4);
            const char* source =
                *holder
                    ? *static_cast<const char* const*>(*holder)
                    : "";
            const char* translated =
                TranslateRuntimeText(source);
            const bool shouldPrepare =
                source &&
                (translated != source ||
                 g_translatedValues.find(source) !=
                     g_translatedValues.end());
            if (!shouldPrepare) {
                continue;
            }
            if (translated != source) {
                TranslatePropertyHolder(drawableBytes + 4);
            }
            int size[2] = {
                *reinterpret_cast<int*>(drawableBytes + 84),
                *reinterpret_cast<int*>(drawableBytes + 88),
            };
            g_originalCaptionPrepare(
                drawable,
                static_cast<int>(
                    reinterpret_cast<uintptr_t>(
                        drawableBytes + 36)),
                size,
                0);
            ++refreshedCount;
        }
    }
    if (refreshedCount != 0) {
        wchar_t message[160] = {};
        _snwprintf_s(
            message,
            _countof(message),
            _TRUNCATE,
            L"refreshed %u live UI caption caches",
            static_cast<unsigned>(refreshedCount));
        WriteLog(message);
    }
    static LONG captionStatsLogs = 0;
    if (InterlockedIncrement(&captionStatsLogs) <= 20) {
        wchar_t message[220] = {};
        _snwprintf_s(
            message,
            _countof(message),
            _TRUNCATE,
            L"live caption stats: objects=%u refreshed=%u",
            static_cast<unsigned>(count),
            static_cast<unsigned>(refreshedCount));
        WriteLog(message);
    }
}

void* __fastcall HookBuilderStart(void* self, void*, const char* source) {
    return g_originalBuilderStart(self, source);
}

void* __fastcall HookBuilderAppend(void* self, void*, const char* source) {
    return g_originalBuilderAppend(self, source);
}

void TranslatePropertyHolder(void* destination) {
    if (!destination || !g_setPropertyString) {
        return;
    }
    const auto* holder = static_cast<const void* const*>(destination);
    const char* source =
        *holder ? *static_cast<const char* const*>(*holder) : "";
    const char* translated = TranslateRuntimeText(source);
    if (translated != source) {
        LogTranslationHit(source, translated);
        g_setPropertyString(destination, translated);
    }
}

char __fastcall HookPropertyString(
    void* self,
    void*,
    const char* propertyName,
    void* destination,
    int optional,
    int searchParent) {
    const char result = g_originalPropertyString(
        self, propertyName, destination, optional, searchParent);
    if (!result || !propertyName || !destination ||
        std::strcmp(propertyName, "caption") != 0 ||
        !g_setPropertyString) {
        return result;
    }

    return result;
}

int __fastcall HookPropertySerialise(void* self, void*) {
    const int result = g_originalPropertySerialise(self);
    return result;
}

void* InstallInlineHook(
    unsigned char* target, void* detour, size_t stolenLength) {
    if (stolenLength < 5) {
        return nullptr;
    }
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(
        nullptr,
        stolenLength + 5,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (!trampoline) {
        wchar_t message[160] = {};
        _snwprintf_s(
            message,
            _countof(message),
            _TRUNCATE,
            L"VirtualAlloc for hook trampoline failed: error=%u",
            GetLastError());
        WriteLog(message);
        return nullptr;
    }
    std::memcpy(trampoline, target, stolenLength);
    trampoline[stolenLength] = 0xE9;
    *reinterpret_cast<DWORD*>(trampoline + stolenLength + 1) =
        static_cast<DWORD>(
            target + stolenLength - (trampoline + stolenLength + 5));

    DWORD oldProtect = 0;
    if (!VirtualProtect(target, stolenLength, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        const DWORD error = GetLastError();
        std::vector<unsigned char> patch(stolenLength, 0x90);
        patch[0] = 0xE9;
        *reinterpret_cast<DWORD*>(patch.data() + 1) =
            static_cast<DWORD>(
                static_cast<unsigned char*>(detour) - (target + 5));
        if (VirtualProtect(
                target,
                stolenLength,
                PAGE_EXECUTE_WRITECOPY,
                &oldProtect)) {
            std::memcpy(target, patch.data(), patch.size());
            DWORD ignored = 0;
            VirtualProtect(target, stolenLength, oldProtect, &ignored);
            FlushInstructionCache(
                GetCurrentProcess(), target, stolenLength);
            return trampoline;
        }
        SIZE_T written = 0;
        if (WriteProcessMemory(
                GetCurrentProcess(),
                target,
                patch.data(),
                patch.size(),
                &written) &&
            written == patch.size()) {
            FlushInstructionCache(
                GetCurrentProcess(), target, stolenLength);
            wchar_t message[220] = {};
            _snwprintf_s(
                message,
                _countof(message),
                _TRUNCATE,
                L"installed COW hook at 0x%08X after VirtualProtect error=%u",
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(target)),
                error);
            WriteLog(message);
            return trampoline;
        }
        const DWORD writeError = GetLastError();
        VirtualFree(trampoline, 0, MEM_RELEASE);
        wchar_t message[200] = {};
        _snwprintf_s(
            message,
            _countof(message),
            _TRUNCATE,
            L"hook write failed at 0x%08X: protect=%u write=%u",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(target)),
            error,
            writeError);
        WriteLog(message);
        return nullptr;
    }
    target[0] = 0xE9;
    *reinterpret_cast<DWORD*>(target + 1) =
        static_cast<DWORD>(
            static_cast<unsigned char*>(detour) - (target + 5));
    for (size_t index = 5; index < stolenLength; ++index) {
        target[index] = 0x90;
    }
    DWORD ignored = 0;
    VirtualProtect(target, stolenLength, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, stolenLength);
    return trampoline;
}

bool ReapplyInlineJump(
    unsigned char* target, void* detour, size_t stolenLength) {
    if (!target || !detour || stolenLength < 5) {
        return false;
    }
    std::vector<unsigned char> patch(stolenLength, 0x90);
    patch[0] = 0xE9;
    *reinterpret_cast<DWORD*>(patch.data() + 1) =
        static_cast<DWORD>(
            static_cast<unsigned char*>(detour) - (target + 5));
    DWORD oldProtect = 0;
    if (VirtualProtect(
            target,
            stolenLength,
            PAGE_EXECUTE_READWRITE,
            &oldProtect)) {
        std::memcpy(target, patch.data(), patch.size());
        DWORD ignored = 0;
        VirtualProtect(target, stolenLength, oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), target, stolenLength);
        return true;
    }
    if (VirtualProtect(
            target,
            stolenLength,
            PAGE_EXECUTE_WRITECOPY,
            &oldProtect)) {
        std::memcpy(target, patch.data(), patch.size());
        DWORD ignored = 0;
        VirtualProtect(target, stolenLength, oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), target, stolenLength);
        return true;
    }
    SIZE_T written = 0;
    if (!WriteProcessMemory(
            GetCurrentProcess(),
            target,
            patch.data(),
            patch.size(),
            &written) ||
        written != patch.size()) {
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), target, stolenLength);
    return true;
}

bool InstallPropertySerialiseVtableHook(unsigned char* base) {
    if (!base) {
        return false;
    }
    auto** slot = reinterpret_cast<void**>(base + 0x34C5BC);
    void* current = nullptr;
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(
            GetCurrentProcess(),
            slot,
            &current,
            sizeof(current),
            &bytesRead) ||
        bytesRead != sizeof(current)) {
        return false;
    }
    void* const detour = reinterpret_cast<void*>(&HookPropertySerialise);
    if (current == detour) {
        return g_originalPropertySerialise != nullptr;
    }
    void* const expected = base + 0x101D90;
    if (current != expected) {
        wchar_t message[220] = {};
        _snwprintf_s(
            message,
            _countof(message),
            _TRUNCATE,
            L"caption property vtable mismatch: slot=0x%08X value=0x%08X expected=0x%08X",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(slot)),
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(current)),
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(expected)));
        WriteLog(message);
        return false;
    }

    DWORD oldProtect = 0;
    bool written = false;
    if (VirtualProtect(
            slot, sizeof(*slot), PAGE_EXECUTE_READWRITE, &oldProtect) ||
        VirtualProtect(
            slot, sizeof(*slot), PAGE_EXECUTE_WRITECOPY, &oldProtect)) {
        *slot = detour;
        DWORD ignored = 0;
        VirtualProtect(slot, sizeof(*slot), oldProtect, &ignored);
        written = true;
    } else {
        SIZE_T bytesWritten = 0;
        written = WriteProcessMemory(
                      GetCurrentProcess(),
                      slot,
                      &detour,
                      sizeof(detour),
                      &bytesWritten) &&
                  bytesWritten == sizeof(detour);
    }
    if (!written) {
        WriteLog(L"failed to patch caption property serialise vtable");
        return false;
    }
    g_originalPropertySerialise =
        reinterpret_cast<PropertySerialiseType>(current);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    WriteLog(L"installed caption property serialise vtable hook");
    return true;
}

bool MatchMasked(
    const unsigned char* address,
    const unsigned char* pattern,
    const char* mask) {
    for (size_t index = 0; mask[index] != '\0'; ++index) {
        if (mask[index] == 'x' && address[index] != pattern[index]) {
            return false;
        }
    }
    return true;
}

bool IsReadableCode(const unsigned char* address, size_t length) {
    MEMORY_BASIC_INFORMATION memory = {};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    const uintptr_t end =
        reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    return reinterpret_cast<uintptr_t>(address) + length <= end;
}

bool MatchRemote(
    const unsigned char* address,
    const unsigned char* pattern,
    const char* mask,
    size_t patternBytes) {
    if (!address || !pattern || !mask || patternBytes > 64) {
        return false;
    }
    unsigned char buffer[64] = {};
    SIZE_T bytesRead = 0;
    return ReadProcessMemory(
               GetCurrentProcess(),
               address,
               buffer,
               patternBytes,
               &bytesRead) &&
           bytesRead == patternBytes &&
           MatchMasked(buffer, pattern, mask);
}

void TryInstallFinalRuntimeHooks(
    unsigned char* protectedAddress, size_t protectedBytes) {
    if (InterlockedCompareExchange(&g_runtimeHooksInstalled, 0, 0) == 1) {
        return;
    }
    static const unsigned char captionPattern[] = {
        0x55, 0x8B, 0xEC, 0x56, 0xFF, 0x75, 0x08, 0x8B, 0xF1,
        0x8D, 0x4D, 0x08, 0xE8, 0xBA, 0xC2, 0x00, 0x00, 0x50,
    };
    if (protectedAddress && protectedBytes >= sizeof(captionPattern)) {
        const size_t limit = protectedBytes - sizeof(captionPattern) + 1;
        for (size_t offset = 0; offset < limit; ++offset) {
            unsigned char* hit = protectedAddress + offset;
            if (std::memcmp(
                    hit, captionPattern, sizeof(captionPattern)) == 0 &&
                reinterpret_cast<uintptr_t>(hit) >= 0x92E7A) {
                g_candidateRuntimeBase = hit - 0x92E7A;
                break;
            }
        }
    }
    unsigned char* base = g_candidateRuntimeBase;
    if (!base) {
        return;
    }
    static const unsigned char advisorPattern[] = {
        0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0, 0, 0, 0,
        0x64, 0xA1, 0, 0, 0, 0, 0x50, 0x83, 0xEC, 0x14,
        0x53, 0x56, 0x57,
    };
    static const unsigned char builderStartPattern[] = {
        0x55, 0x8B, 0xEC, 0x56, 0x57, 0x8B, 0xF9, 0x8B,
        0x4D, 0x08, 0x8B, 0xF1, 0x8D, 0x56, 0x01, 0x90,
    };
    static const unsigned char builderAppendPattern[] = {
        0x55, 0x8B, 0xEC, 0x8B, 0x55, 0x08, 0x56, 0x8B,
        0xF2, 0x57, 0x8B, 0xF9, 0x8D, 0x4E, 0x01, 0x90,
    };
    static const unsigned char propertyStringPattern[] = {
        0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68,
    };
    static const unsigned char captionPreparePattern[] = {
        0x55, 0x8B, 0xEC, 0x51, 0x51,
    };
    static const unsigned char fontPrepareStringPattern[] = {
        0x55, 0x8B, 0xEC, 0x83, 0x3D, 0, 0, 0, 0, 0,
    };
    static const unsigned char captionDrawPattern[] = {
        0x55, 0x8B, 0xEC, 0x51, 0x56,
    };
    static const unsigned char primitiveQueuePattern[] = {
        0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18,
    };
    static const unsigned char labelFactoryPattern[] = {
        0x68, 0xE4, 0x00, 0x00, 0x00,
    };
    static const unsigned char buttonFactoryPattern[] = {
        0x68, 0xF8, 0x00, 0x00, 0x00,
    };
    if (!IsReadableCode(base + 0x92AF0, sizeof(labelFactoryPattern)) ||
        !IsReadableCode(base + 0x931D8, sizeof(buttonFactoryPattern)) ||
        !IsReadableCode(base + 0x946CC, sizeof(captionDrawPattern)) ||
        !IsReadableCode(
            base + 0x6D622,
            sizeof(fontPrepareStringPattern)) ||
        !IsReadableCode(
            base + 0xB49F1,
            sizeof(primitiveQueuePattern)) ||
        !IsReadableCode(base + 0x947DC, sizeof(captionPreparePattern)) ||
        !IsReadableCode(base + 0x101750, sizeof(propertyStringPattern)) ||
        !IsReadableCode(base + 0x10B470, sizeof(advisorPattern)) ||
        !IsReadableCode(base + 0x161430, sizeof(builderStartPattern)) ||
        !IsReadableCode(base + 0x161550, sizeof(builderAppendPattern)) ||
        !MatchMasked(
            base + 0x92AF0, labelFactoryPattern, "xxxxx") ||
        !MatchMasked(
            base + 0x931D8, buttonFactoryPattern, "xxxxx") ||
        !MatchMasked(
            base + 0x946CC, captionDrawPattern, "xxxxx") ||
        !MatchMasked(
            base + 0x6D622,
            fontPrepareStringPattern,
            "xxxxx????x") ||
        !MatchMasked(
            base + 0xB49F1,
            primitiveQueuePattern,
            "xxxxxx") ||
        !MatchMasked(
            base + 0x947DC, captionPreparePattern, "xxxxx") ||
        !MatchMasked(
            base + 0x101750, propertyStringPattern, "xxxxxx") ||
        !MatchMasked(
            base + 0x10B470, advisorPattern, "xxxxxx????xx????xxxxxxx") ||
        !MatchMasked(
            base + 0x161430, builderStartPattern, "xxxxxxxxxxxxxxxx") ||
        !MatchMasked(
            base + 0x161550, builderAppendPattern, "xxxxxxxxxxxxxxxx")) {
        return;
    }
    if (InterlockedCompareExchange(&g_runtimeHooksInstalled, -1, 0) != 0) {
        return;
    }
    g_runtimeImageBase = base;
    g_setPropertyString = reinterpret_cast<SetPropertyStringType>(
        base + 0x9F325);
    g_captionPropertyHash =
        reinterpret_cast<HashStringType>(base + 0x13F9A)("caption");
    const bool propertySerialiseInstalled =
        InstallPropertySerialiseVtableHook(base);
    g_originalCaptionPrepare =
        reinterpret_cast<CaptionPrepareType>(
            InstallInlineHook(base + 0x947DC, &HookCaptionPrepare, 5));
    g_originalCaptionDraw =
        reinterpret_cast<CaptionDrawType>(
            InstallInlineHook(base + 0x946CC, &HookCaptionDraw, 5));
    g_originalFontPrepareString =
        reinterpret_cast<FontPrepareStringType>(
            InstallInlineHook(
                base + 0x6D622,
                &HookFontPrepareString,
                10));
    g_originalPrimitiveQueue =
        reinterpret_cast<PrimitiveQueueType>(
            InstallInlineHook(
                base + 0xB49F1,
                &HookPrimitiveQueue,
                6));
    g_originalLabelFactory =
        reinterpret_cast<UIFactoryType>(
            InstallInlineHook(base + 0x92AF0, &HookLabelFactory, 5));
    g_originalButtonFactory =
        reinterpret_cast<UIFactoryType>(
            InstallInlineHook(base + 0x931D8, &HookButtonFactory, 5));
    g_originalPropertyString = reinterpret_cast<PropertyStringType>(
        InstallInlineHook(base + 0x101750, &HookPropertyString, 5));
    g_originalAdvisorText = reinterpret_cast<AdvisorTextType>(
        InstallInlineHook(base + 0x10B470, &HookAdvisorText, 5));
    g_originalBuilderStart = reinterpret_cast<TextBuilderType>(
        InstallInlineHook(base + 0x161430, &HookBuilderStart, 5));
    g_originalBuilderAppend = reinterpret_cast<TextBuilderType>(
        InstallInlineHook(base + 0x161550, &HookBuilderAppend, 6));
    const bool installed =
        propertySerialiseInstalled &&
        g_originalCaptionPrepare && g_originalCaptionDraw &&
        g_originalFontPrepareString &&
        g_originalPrimitiveQueue &&
        g_originalLabelFactory && g_originalButtonFactory &&
        g_originalPropertyString &&
        g_originalAdvisorText &&
        g_originalBuilderStart && g_originalBuilderAppend;
    InterlockedExchange(&g_runtimeHooksInstalled, installed ? 1 : 0);
    wchar_t message[200] = {};
    _snwprintf_s(
        message,
        _countof(message),
        _TRUNCATE,
        installed
            ? L"installed final runtime hooks at s86 base 0x%08X"
            : L"failed to install final runtime hooks at s86 base 0x%08X",
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(base)));
    WriteLog(message);
}

BOOL WINAPI HookVirtualProtect(
    LPVOID address,
    SIZE_T bytes,
    DWORD newProtection,
    PDWORD oldProtection) {
    const BOOL result =
        g_originalVirtualProtect(address, bytes, newProtection, oldProtection);
    const DWORD executable =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
        PAGE_EXECUTE_WRITECOPY;
    if (result && (newProtection & executable) != 0 && bytes >= 0x1000) {
        static LONG protectionLogCount = 0;
        if (InterlockedIncrement(&protectionLogCount) <= 20) {
            wchar_t message[200] = {};
            _snwprintf_s(
                message,
                _countof(message),
                _TRUNCATE,
                L"observed executable VirtualProtect: address=0x%08X bytes=0x%X protection=0x%X",
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(address)),
                static_cast<unsigned>(bytes),
                newProtection);
            WriteLog(message);
        }
        TryInstallFinalRuntimeHooks(
            static_cast<unsigned char*>(address), bytes);
    }
    return result;
}

LPVOID WINAPI HookVirtualAlloc(
    LPVOID address, SIZE_T bytes, DWORD allocationType, DWORD protection) {
    LPVOID result =
        g_originalVirtualAlloc(address, bytes, allocationType, protection);
    if (result && bytes >= 0x10000) {
        const LONG index = InterlockedIncrement(&g_runtimeAllocationCount) - 1;
        if (index >= 0 && index < static_cast<LONG>(_countof(g_runtimeAllocations))) {
            g_runtimeAllocations[index] = {
                static_cast<unsigned char*>(result), bytes};
        }
        if (index < 20) {
            wchar_t message[200] = {};
            _snwprintf_s(
                message,
                _countof(message),
                _TRUNCATE,
                L"observed VirtualAlloc: address=0x%08X bytes=0x%X protection=0x%X",
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(result)),
                static_cast<unsigned>(bytes),
                protection);
            WriteLog(message);
        }
    }
    return result;
}

LPVOID WINAPI HookMapViewOfFile(
    HANDLE mapping,
    DWORD desiredAccess,
    DWORD fileOffsetHigh,
    DWORD fileOffsetLow,
    SIZE_T bytesToMap) {
    LPVOID result = g_originalMapViewOfFile(
        mapping,
        desiredAccess,
        fileOffsetHigh,
        fileOffsetLow,
        bytesToMap);
    if (!result ||
        InterlockedCompareExchange(&g_runtimeHooksInstalled, 0, 0) != 0) {
        return result;
    }
    MEMORY_BASIC_INFORMATION memory = {};
    if (VirtualQuery(result, &memory, sizeof(memory)) == sizeof(memory) &&
        memory.AllocationBase) {
        auto* base = static_cast<unsigned char*>(memory.AllocationBase);
        g_candidateRuntimeBase = base;
        TryInstallFinalRuntimeHooks(nullptr, 0);
        if (InterlockedCompareExchange(
                &g_runtimeHooksInstalled, 0, 0) != 1) {
            g_hardwareTargets[0] =
                reinterpret_cast<uintptr_t>(base + 0x92E7A);
            g_hardwareTargets[1] =
                reinterpret_cast<uintptr_t>(base + 0x10B470);
            g_hardwareTargets[2] =
                reinterpret_cast<uintptr_t>(base + 0x161430);
            g_hardwareTargets[3] =
                reinterpret_cast<uintptr_t>(base + 0x161550);
            InterlockedExchange(&g_hardwareBreakpointsReady, 1);
            g_earlyTargetThreadId = GetCurrentThreadId();
            for (int attempt = 0; attempt < 200; ++attempt) {
                if (InterlockedCompareExchange(
                        &g_earlyBreakpointsApplied, 0, 0) != 0) {
                    break;
                }
                Sleep(10);
            }
        }
    }
    return result;
}

FARPROC WINAPI HookGetProcAddress(HMODULE module, LPCSTR name) {
    if (name &&
        reinterpret_cast<ULONG_PTR>(name) > 0xFFFF &&
        std::strcmp(name, "MapViewOfFile") == 0) {
        return reinterpret_cast<FARPROC>(&HookMapViewOfFile);
    }
    return g_originalGetProcAddress(module, name);
}

VOID WINAPI HookSleep(DWORD milliseconds) {
    EnsureRuntimeHooksFromGameThread();
    g_originalSleep(milliseconds);
}

DWORD WINAPI HookGetTickCount() {
    EnsureRuntimeHooksFromGameThread();
    return g_originalGetTickCount();
}

BOOL WINAPI HookQueryPerformanceCounter(LARGE_INTEGER* counter) {
    EnsureRuntimeHooksFromGameThread();
    return g_originalQueryPerformanceCounter(counter);
}

LRESULT WINAPI HookDispatchMessageA(const MSG* message) {
    EnsureRuntimeHooksFromGameThread();
    return g_originalDispatchMessageA(message);
}

struct InPlaceTranslation {
    const std::string* source;
    const std::string* translated;
};

unsigned int FirstFourBytes(const std::string& value) {
    unsigned int key = 0;
    std::memcpy(&key, value.data(), sizeof(key));
    return key;
}

size_t PatchRuntimeStringRegion(
    unsigned char* region,
    size_t regionBytes,
    const std::unordered_map<
        unsigned int, std::vector<InPlaceTranslation>>& buckets) {
    if (!region || regionBytes < 5) {
        return 0;
    }
    size_t maximumSourceBytes = 4;
    std::vector<InPlaceTranslation> candidates;
    for (const auto& bucket : buckets) {
        for (const InPlaceTranslation& candidate : bucket.second) {
            maximumSourceBytes =
                max(maximumSourceBytes, candidate.source->size());
            candidates.push_back(candidate);
        }
    }
    const size_t chunkBytes = 1024 * 1024;
    std::vector<unsigned char> snapshot(
        chunkBytes + maximumSourceBytes + 1);
    size_t patched = 0;
    for (size_t chunkOffset = 0;
         chunkOffset < regionBytes;
         chunkOffset += chunkBytes) {
        const size_t primaryBytes =
            min(chunkBytes, regionBytes - chunkOffset);
        const size_t requestedBytes = min(
            primaryBytes + maximumSourceBytes + 1,
            regionBytes - chunkOffset);
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(
                GetCurrentProcess(),
                region + chunkOffset,
                snapshot.data(),
                requestedBytes,
                &bytesRead) ||
            bytesRead < 5) {
            continue;
        }
        const size_t scanBytes = min(primaryBytes, static_cast<size_t>(bytesRead));
        for (const InPlaceTranslation& candidate : candidates) {
            const unsigned char first =
                static_cast<unsigned char>((*candidate.source)[0]);
            size_t offset = 0;
            while (offset < scanBytes) {
                const void* found = std::memchr(
                    snapshot.data() + offset,
                    first,
                    scanBytes - offset);
                if (!found) {
                    break;
                }
                offset = static_cast<const unsigned char*>(found) -
                         snapshot.data();
                const size_t sourceBytes = candidate.source->size();
                if (offset + sourceBytes >= bytesRead ||
                    snapshot[offset + sourceBytes] != '\0' ||
                    std::memcmp(
                        snapshot.data() + offset,
                        candidate.source->data(),
                        sourceBytes) != 0) {
                    ++offset;
                    continue;
                }
                std::vector<unsigned char> replacement(sourceBytes + 1, 0);
                std::memcpy(
                    replacement.data(),
                    candidate.translated->data(),
                    candidate.translated->size());
                SIZE_T bytesWritten = 0;
                if (!WriteProcessMemory(
                        GetCurrentProcess(),
                        region + chunkOffset + offset,
                        replacement.data(),
                        replacement.size(),
                        &bytesWritten) ||
                    bytesWritten != replacement.size()) {
                    ++offset;
                    continue;
                }
                ++patched;
                offset += sourceBytes + 1;
            }
        }
    }
    return patched;
}

bool PatchUnpackedRuntimeStrings() {
    if (InterlockedCompareExchange(&g_runtimeStringsPatched, -1, 0) != 0) {
        return false;
    }
    std::unordered_map<unsigned int, std::vector<InPlaceTranslation>> buckets;
    size_t eligible = 0;
    for (const auto& entry : g_translations) {
        const bool diagnosticTarget =
            entry.first == "Proceed" ||
            entry.first.rfind(
                "Let\xE2\x80\x99s check on the neighbors.", 0) == 0 ||
            entry.first.rfind(
                "Sacred Time is two weeks of magical ceremonies", 0) == 0;
        if (!diagnosticTarget) {
            continue;
        }
        if (entry.first.size() < 4 ||
            entry.second.size() > entry.first.size() ||
            entry.first.find('\0') != std::string::npos ||
            entry.second.find('\0') != std::string::npos) {
            continue;
        }
        buckets[FirstFourBytes(entry.first)].push_back(
            {&entry.first, &entry.second});
        ++eligible;
    }

    size_t patched = 0;
    WriteLog(L"starting in-place runtime translation scan");
    const LONG count = min(
        InterlockedCompareExchange(&g_runtimeAllocationCount, 0, 0),
        static_cast<LONG>(_countof(g_runtimeAllocations)));
    for (LONG index = 0; index < count; ++index) {
        const RuntimeAllocation allocation = g_runtimeAllocations[index];
        if (!allocation.address || allocation.bytes < 16 * 1024 * 1024) {
            continue;
        }
        MEMORY_BASIC_INFORMATION first = {};
        if (VirtualQuery(
                allocation.address, &first, sizeof(first)) != sizeof(first)) {
            continue;
        }
        auto* cursor = static_cast<unsigned char*>(first.AllocationBase);
        while (true) {
            MEMORY_BASIC_INFORMATION memory = {};
            if (VirtualQuery(cursor, &memory, sizeof(memory)) != sizeof(memory) ||
                memory.AllocationBase != first.AllocationBase) {
                break;
            }
            const DWORD blocked = PAGE_GUARD | PAGE_NOACCESS;
            const DWORD writable =
                PAGE_READWRITE | PAGE_WRITECOPY |
                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            if (memory.State == MEM_COMMIT &&
                (memory.Protect & blocked) == 0 &&
                (memory.Protect & writable) != 0) {
                patched += PatchRuntimeStringRegion(
                    static_cast<unsigned char*>(memory.BaseAddress),
                    memory.RegionSize,
                    buckets);
            }
            auto* next =
                static_cast<unsigned char*>(memory.BaseAddress) +
                memory.RegionSize;
            if (next <= cursor) {
                break;
            }
            cursor = next;
        }
    }
    InterlockedExchange(
        &g_runtimeStringsPatched,
        0);
    wchar_t message[220] = {};
    _snwprintf_s(
        message,
        _countof(message),
        _TRUNCATE,
        L"in-place runtime translation: eligible=%u patched=%u",
        static_cast<unsigned>(eligible),
        static_cast<unsigned>(patched));
    WriteLog(message);
    return patched > 0;
}

LONG WINAPI HandleTextBreakpoint(EXCEPTION_POINTERS* exception) {
#if !defined(_M_IX86)
    return EXCEPTION_CONTINUE_SEARCH;
#else
    if (!exception || !exception->ExceptionRecord || !exception->ContextRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
        InterlockedCompareExchange(
            &g_hardwareBreakpointsReady, 0, 0) != 0) {
        CONTEXT* context = exception->ContextRecord;
        const uintptr_t instruction = context->Eip;
        bool recognized = false;
        for (uintptr_t target : g_hardwareTargets) {
            recognized = recognized || instruction == target;
        }
        if (!recognized) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        context->Dr6 = 0;
        context->EFlags |= 0x10000;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    auto* address = static_cast<unsigned char*>(
        exception->ExceptionRecord->ExceptionAddress);
    MEMORY_BASIC_INFORMATION memory = {};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    static const unsigned char captionTail[] = {
        0x8B, 0xEC, 0x56, 0xFF, 0x75, 0x08, 0x8B, 0xF1,
        0x8D, 0x4D, 0x08, 0xE8,
    };
    static const unsigned char advisorTail[] = {
        0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0, 0, 0, 0, 0x64, 0xA1,
    };
    static const unsigned char builderStartTail[] = {
        0x8B, 0xEC, 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x4D,
        0x08, 0x8B, 0xF1, 0x8D, 0x56, 0x01, 0x90,
    };
    static const unsigned char builderAppendTail[] = {
        0x8B, 0xEC, 0x8B, 0x55, 0x08, 0x56, 0x8B, 0xF2,
        0x57, 0x8B, 0xF9, 0x8D, 0x4E, 0x01, 0x90,
    };
    const bool recognized =
        MatchMasked(address + 1, captionTail, "xxxxxxxxxxxx") ||
        MatchMasked(address + 1, advisorTail, "xxxxx????xx") ||
        MatchMasked(address + 1, builderStartTail, "xxxxxxxxxxxxxxx") ||
        MatchMasked(address + 1, builderAppendTail, "xxxxxxxxxxxxxxx");
    if (!recognized) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* context = exception->ContextRecord;
    context->Esp -= sizeof(DWORD);
    *reinterpret_cast<DWORD*>(context->Esp) = context->Ebp;
    context->Eip =
        static_cast<DWORD>(reinterpret_cast<uintptr_t>(address + 1));
    return EXCEPTION_CONTINUE_EXECUTION;
#endif
}

unsigned char* FindRuntimeImageBase() {
    static const unsigned char captionPattern[] = {
        0x55, 0x8B, 0xEC, 0x56, 0xFF, 0x75, 0x08, 0x8B, 0xF1,
        0x8D, 0x4D, 0x08, 0xE8, 0xBA, 0xC2, 0x00, 0x00, 0x50,
    };
    static const char captionMask[] = "xxxxxxxxxxxxxxxxxx";
    static const unsigned char advisorPattern[] = {
        0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0, 0, 0, 0,
        0x64, 0xA1, 0, 0, 0, 0, 0x50, 0x83, 0xEC, 0x14,
        0x53, 0x56, 0x57,
    };
    static const char advisorMask[] = "xxxxxx????xx????xxxxxxx";
    static const unsigned char builderStartPattern[] = {
        0x55, 0x8B, 0xEC, 0x56, 0x57, 0x8B, 0xF9, 0x8B,
        0x4D, 0x08, 0x8B, 0xF1, 0x8D, 0x56, 0x01, 0x90,
    };
    static const char builderStartMask[] = "xxxxxxxxxxxxxxxx";
    static const unsigned char builderAppendPattern[] = {
        0x55, 0x8B, 0xEC, 0x8B, 0x55, 0x08, 0x56, 0x8B,
        0xF2, 0x57, 0x8B, 0xF9, 0x8D, 0x4E, 0x01, 0x90,
    };
    static const char builderAppendMask[] = "xxxxxxxxxxxxxxxx";
    static const unsigned char propertyStringPattern[] = {
        0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68,
    };
    static const char propertyStringMask[] = "xxxxxx";

    SYSTEM_INFO systemInfo = {};
    GetSystemInfo(&systemInfo);
    auto* cursor =
        static_cast<unsigned char*>(systemInfo.lpMinimumApplicationAddress);
    auto* maximum =
        static_cast<unsigned char*>(systemInfo.lpMaximumApplicationAddress);
    void* lastAllocationBase = nullptr;
    while (cursor < maximum) {
        MEMORY_BASIC_INFORMATION memory = {};
        if (VirtualQuery(cursor, &memory, sizeof(memory)) != sizeof(memory)) {
            break;
        }
        if (memory.AllocationBase &&
            memory.AllocationBase != lastAllocationBase) {
            lastAllocationBase = memory.AllocationBase;
            auto* base =
                static_cast<unsigned char*>(memory.AllocationBase);
            if (MatchRemote(
                    base + 0x92E7A,
                    captionPattern,
                    captionMask,
                    sizeof(captionPattern)) &&
                MatchRemote(
                    base + 0x101750,
                    propertyStringPattern,
                    propertyStringMask,
                    sizeof(propertyStringPattern)) &&
                MatchRemote(
                    base + 0x10B470,
                    advisorPattern,
                    advisorMask,
                    sizeof(advisorPattern)) &&
                MatchRemote(
                    base + 0x161430,
                    builderStartPattern,
                    builderStartMask,
                    sizeof(builderStartPattern)) &&
                MatchRemote(
                    base + 0x161550,
                    builderAppendPattern,
                    builderAppendMask,
                    sizeof(builderAppendPattern))) {
                return base;
            }
        }
        auto* next =
            static_cast<unsigned char*>(memory.BaseAddress) +
            memory.RegionSize;
        if (next <= cursor) {
            break;
        }
        cursor = next;
    }
    return nullptr;
}

void EnsureRuntimeHooksFromGameThread() {
    static DWORD lastCheck = 0;
    const DWORD now = GetTickCount();
    if (now - lastCheck < 100) {
        return;
    }
    lastCheck = now;
    unsigned char* base = g_candidateRuntimeBase;
    if (!base) {
        base = FindRuntimeImageBase();
    }
    if (base &&
        (base != g_candidateRuntimeBase ||
         InterlockedCompareExchange(
             &g_runtimeHooksInstalled, 0, 0) != 1)) {
        g_candidateRuntimeBase = base;
        InterlockedExchange(&g_runtimeHooksInstalled, 0);
        TryInstallFinalRuntimeHooks(nullptr, 0);
    }
    unsigned char* current = g_candidateRuntimeBase;
    if (current &&
        InterlockedCompareExchange(
            &g_runtimeHooksInstalled, 0, 0) == 1) {
        InstallPropertySerialiseVtableHook(current);
        const struct {
            size_t offset;
            void* detour;
            size_t stolenLength;
        } hooks[] = {
            {0x92AF0, &HookLabelFactory, 5},
            {0x931D8, &HookButtonFactory, 5},
            {0x946CC, &HookCaptionDraw, 5},
            {0xB49F1, &HookPrimitiveQueue, 6},
            {0x947DC, &HookCaptionPrepare, 5},
            {0x101750, &HookPropertyString, 5},
            {0x10B470, &HookAdvisorText, 5},
            {0x161430, &HookBuilderStart, 5},
            {0x161550, &HookBuilderAppend, 6},
        };
        for (const auto& hook : hooks) {
            unsigned char marker = 0;
            SIZE_T bytesRead = 0;
            if (ReadProcessMemory(
                    GetCurrentProcess(),
                    current + hook.offset,
                    &marker,
                    sizeof(marker),
                    &bytesRead) &&
                bytesRead == sizeof(marker) &&
                marker != 0xE9) {
                if (ReapplyInlineJump(
                    current + hook.offset,
                    hook.detour,
                    hook.stolenLength)) {
                    static LONG ensureReapplyLogs = 0;
                    if (InterlockedIncrement(&ensureReapplyLogs) <= 12) {
                        WriteLog(L"watchdog reapplied a runtime hook");
                    }
                }
            }
        }
    }
}

int ApplyHardwareBreakpointsToGameThreads() {
#if !defined(_M_IX86)
    return 0;
#else
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    const DWORD processId = GetCurrentProcessId();
    const DWORD currentThreadId = GetCurrentThreadId();
    int applied = 0;
    THREADENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != processId ||
                entry.th32ThreadID == currentThreadId) {
                continue;
            }
            HANDLE thread = OpenThread(
                THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                    THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                FALSE,
                entry.th32ThreadID);
            if (!thread) {
                continue;
            }
            if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
                CONTEXT context = {};
                context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (GetThreadContext(thread, &context)) {
                    context.Dr0 = g_hardwareTargets[0];
                    context.Dr1 = g_hardwareTargets[1];
                    context.Dr2 = g_hardwareTargets[2];
                    context.Dr3 = g_hardwareTargets[3];
                    context.Dr6 = 0;
                    context.Dr7 &= ~static_cast<DWORD_PTR>(0xFFFF00FF);
                    context.Dr7 |= 0x55;
                    if (SetThreadContext(thread, &context)) {
                        ++applied;
                    }
                }
                ResumeThread(thread);
            }
            CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return applied;
#endif
}

DWORD WINAPI InstallEarlyBreakpoints(void*) {
#if !defined(_M_IX86)
    const int applied = 0;
#else
    int applied = 0;
    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
            THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_earlyTargetThreadId);
    if (thread &&
        SuspendThread(thread) != static_cast<DWORD>(-1)) {
        CONTEXT context = {};
        context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(thread, &context)) {
            context.Dr0 = g_hardwareTargets[0];
            context.Dr1 = g_hardwareTargets[1];
            context.Dr2 = g_hardwareTargets[2];
            context.Dr3 = g_hardwareTargets[3];
            context.Dr6 = 0;
            context.Dr7 &= ~static_cast<DWORD_PTR>(0xFFFF00FF);
            context.Dr7 |= 0x55;
            if (SetThreadContext(thread, &context)) {
                applied = 1;
            }
        }
        ResumeThread(thread);
    }
    if (thread) {
        CloseHandle(thread);
    }
#endif
    InterlockedExchange(
        &g_earlyBreakpointsApplied, applied > 0 ? 1 : -1);
    wchar_t message[180] = {};
    _snwprintf_s(
        message,
        _countof(message),
        _TRUNCATE,
        L"installed early hardware text hooks on %d threads",
        applied);
    WriteLog(message);
    return 0;
}

DWORD WINAPI InstallGameHooks(void*) {
    if (!LoadTranslations()) {
        return 1;
    }
    InterlockedExchange(&g_translationsReady, 1);
    WriteLog(L"runtime translation worker ready");
    WriteLog(L"native caption hook mode enabled; overlay disabled");
    unsigned char* runtimeBase = nullptr;
    for (int attempt = 0; attempt < 240 && !runtimeBase; ++attempt) {
        if (g_earlyTargetThreadId != 0 &&
            InterlockedCompareExchange(
                &g_earlyBreakpointsApplied, 0, 0) == 0) {
            InstallEarlyBreakpoints(nullptr);
        }
        runtimeBase = FindRuntimeImageBase();
        if (!runtimeBase) {
            Sleep(50);
        }
    }
    if (runtimeBase) {
        g_candidateRuntimeBase = runtimeBase;
        TryInstallFinalRuntimeHooks(nullptr, 0);
        struct GuardedHook {
            size_t offset;
            void* detour;
            size_t stolenLength;
        };
        const GuardedHook guarded[] = {
            {0x92AF0, &HookLabelFactory, 5},
            {0x931D8, &HookButtonFactory, 5},
            {0x946CC, &HookCaptionDraw, 5},
            {0xB49F1, &HookPrimitiveQueue, 6},
            {0x947DC, &HookCaptionPrepare, 5},
            {0x101750, &HookPropertyString, 5},
            {0x10B470, &HookAdvisorText, 5},
            {0x161430, &HookBuilderStart, 5},
            {0x161550, &HookBuilderAppend, 6},
        };
        LONG reapplied = 0;
        LONG reapplyFailed = 0;
        bool guardEnabled =
            InterlockedCompareExchange(
                &g_runtimeHooksInstalled, 0, 0) == 1;
        WriteLog(L"runtime hook guard started");
        for (int attempt = 0; attempt < 0; ++attempt) {
            if (!guardEnabled &&
                InterlockedCompareExchange(
                    &g_runtimeHooksInstalled, 0, 0) == 1) {
                guardEnabled = true;
                if (g_candidateRuntimeBase) {
                    runtimeBase = g_candidateRuntimeBase;
                }
            }
            if (attempt % 10 == 0) {
                unsigned char* relocatedBase = FindRuntimeImageBase();
                if (relocatedBase &&
                    (relocatedBase != runtimeBase || !guardEnabled)) {
                    runtimeBase = relocatedBase;
                    g_candidateRuntimeBase = runtimeBase;
                    InterlockedExchange(&g_runtimeHooksInstalled, 0);
                    TryInstallFinalRuntimeHooks(nullptr, 0);
                    guardEnabled =
                        InterlockedCompareExchange(
                            &g_runtimeHooksInstalled, 0, 0) == 1;
                }
            }
            if (!guardEnabled) {
                Sleep(10);
                continue;
            }
            for (const GuardedHook& hook : guarded) {
                unsigned char marker = 0;
                SIZE_T markerBytes = 0;
                const bool readable = ReadProcessMemory(
                    GetCurrentProcess(),
                    runtimeBase + hook.offset,
                    &marker,
                    sizeof(marker),
                    &markerBytes) &&
                    markerBytes == sizeof(marker);
                if (readable && marker != 0xE9) {
                    if (ReapplyInlineJump(
                            runtimeBase + hook.offset,
                            hook.detour,
                            hook.stolenLength)) {
                        ++reapplied;
                    } else {
                        ++reapplyFailed;
                    }
                }
            }
            if (attempt > 0 && attempt % 500 == 0) {
                wchar_t heartbeat[220] = {};
                _snwprintf_s(
                    heartbeat,
                    _countof(heartbeat),
                    _TRUNCATE,
                    L"runtime hook guard: base=0x%08X reapplied=%u failed=%u",
                    static_cast<unsigned>(
                        reinterpret_cast<uintptr_t>(runtimeBase)),
                    static_cast<unsigned>(reapplied),
                    static_cast<unsigned>(reapplyFailed));
                WriteLog(heartbeat);
            }
            Sleep(10);
        }
        if (reapplied > 0) {
            wchar_t message[180] = {};
            _snwprintf_s(
                message,
                _countof(message),
                _TRUNCATE,
                L"reapplied %u runtime hooks after loader overwrite",
                static_cast<unsigned>(reapplied));
            WriteLog(message);
        }
    } else {
        WriteLog(L"could not locate executable s86 image for runtime hooks");
    }
    return 0;
}

HANDLE WINAPI HookCreateFileW(
    LPCWSTR fileName,
    DWORD desiredAccess,
    DWORD shareMode,
    LPSECURITY_ATTRIBUTES securityAttributes,
    DWORD creationDisposition,
    DWORD flagsAndAttributes,
    HANDLE templateFile) {
    if (fileName && EndsWith(
            ToLowerPath(fileName), L"king of dragon pass.s86")) {
        wchar_t message[2 * MAX_PATH] = {};
        _snwprintf_s(
            message,
            _countof(message),
            _TRUNCATE,
            L"observed CreateFileW for s86: %s access=0x%08X",
            fileName,
            desiredAccess);
        WriteLog(message);
    }
    wchar_t overlayPath[MAX_PATH] = {};
    if (GetOverlayPath(
            fileName, desiredAccess, overlayPath, _countof(overlayPath))) {
        if (InterlockedIncrement(&g_redirectLogWritten) <= 20) {
            wchar_t message[2 * MAX_PATH] = {};
            _snwprintf_s(
                message,
                _countof(message),
                _TRUNCATE,
                L"redirected %s -> %s",
                fileName,
                overlayPath);
            WriteLog(message);
        }
        return g_originalCreateFileW(
            overlayPath,
            desiredAccess,
            shareMode,
            securityAttributes,
            creationDisposition,
            flagsAndAttributes,
            templateFile);
    }
    HANDLE result = g_originalCreateFileW(
        fileName,
        desiredAccess,
        shareMode,
        securityAttributes,
        creationDisposition,
        flagsAndAttributes,
        templateFile);
    if (result != INVALID_HANDLE_VALUE && IsTemporaryS86(fileName) &&
        (desiredAccess & GENERIC_WRITE) != 0) {
        g_temporaryS86WriteHandle = result;
    }
    if (result != INVALID_HANDLE_VALUE && IsGameS86(fileName)) {
        g_temporaryS86ReadHandle = result;
        wchar_t message[160] = {};
        _snwprintf_s(
            message,
            _countof(message),
            _TRUNCATE,
            L"tracking temporary s86 handle (access=0x%08X)",
            desiredAccess);
        WriteLog(message);
    }
    return result;
}

HANDLE WINAPI HookCreateFileA(
    LPCSTR fileName,
    DWORD desiredAccess,
    DWORD shareMode,
    LPSECURITY_ATTRIBUTES securityAttributes,
    DWORD creationDisposition,
    DWORD flagsAndAttributes,
    HANDLE templateFile) {
    if (fileName) {
        wchar_t widePath[MAX_PATH] = {};
        const int converted = MultiByteToWideChar(
            CP_ACP, 0, fileName, -1, widePath, _countof(widePath));
        if (converted > 0 && EndsWith(
                ToLowerPath(widePath), L"king of dragon pass.s86")) {
            wchar_t message[2 * MAX_PATH] = {};
            _snwprintf_s(
                message,
                _countof(message),
                _TRUNCATE,
                L"observed CreateFileA for s86: %s access=0x%08X",
                widePath,
                desiredAccess);
            WriteLog(message);
        }
        wchar_t overlayPath[MAX_PATH] = {};
        if (converted > 0 &&
            GetOverlayPath(
                widePath, desiredAccess, overlayPath, _countof(overlayPath))) {
            if (InterlockedIncrement(&g_redirectLogWritten) <= 20) {
                wchar_t message[2 * MAX_PATH] = {};
                _snwprintf_s(
                    message,
                    _countof(message),
                    _TRUNCATE,
                    L"redirected %s -> %s",
                    widePath,
                    overlayPath);
                WriteLog(message);
            }
            return g_originalCreateFileW(
                overlayPath,
                desiredAccess,
                shareMode,
                securityAttributes,
                creationDisposition,
                flagsAndAttributes,
                templateFile);
        }
    }
    HANDLE result = g_originalCreateFileA(
        fileName,
        desiredAccess,
        shareMode,
        securityAttributes,
        creationDisposition,
        flagsAndAttributes,
        templateFile);
    if (result != INVALID_HANDLE_VALUE && fileName) {
        wchar_t widePath[MAX_PATH] = {};
        if (MultiByteToWideChar(
                CP_ACP, 0, fileName, -1, widePath, _countof(widePath)) > 0 &&
            IsTemporaryS86(widePath) &&
            (desiredAccess & GENERIC_WRITE) != 0) {
            g_temporaryS86WriteHandle = result;
        }
        if (MultiByteToWideChar(
                CP_ACP, 0, fileName, -1, widePath, _countof(widePath)) > 0 &&
            IsGameS86(widePath)) {
            g_temporaryS86ReadHandle = result;
            wchar_t message[160] = {};
            _snwprintf_s(
                message,
                _countof(message),
                _TRUNCATE,
                L"tracking temporary s86 handle (access=0x%08X)",
                desiredAccess);
            WriteLog(message);
        }
    }
    return result;
}

bool PatchMainExecutableImports() {
    auto* module = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!module) {
        return false;
    }
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    const IMAGE_DATA_DIRECTORY& directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0) {
        return false;
    }

    bool patchedA = false;
    bool patchedW = false;
    bool patchedWrite = false;
    bool patchedRead = false;
    bool patchedVirtualProtect = false;
    bool patchedVirtualAlloc = false;
    bool patchedSleep = false;
    bool patchedGetTickCount = false;
    bool patchedQueryPerformanceCounter = false;
    bool patchedDispatchMessageA = false;
    auto* descriptor =
        reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(module + directory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(
            module + descriptor->OriginalFirstThunk);
        auto* addresses =
            reinterpret_cast<IMAGE_THUNK_DATA*>(module + descriptor->FirstThunk);
        if (descriptor->OriginalFirstThunk == 0) {
            names = addresses;
        }

        for (; names->u1.AddressOfData != 0; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
                continue;
            }
            auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                module + names->u1.AddressOfData);
            void* replacement = nullptr;
            if (std::strcmp(
                    reinterpret_cast<const char*>(import->Name), "CreateFileA") == 0) {
                replacement = reinterpret_cast<void*>(&HookCreateFileA);
                patchedA = true;
            } else if (
                std::strcmp(
                    reinterpret_cast<const char*>(import->Name), "CreateFileW") == 0) {
                replacement = reinterpret_cast<void*>(&HookCreateFileW);
                patchedW = true;
            } else if (
                std::strcmp(
                    reinterpret_cast<const char*>(import->Name), "WriteFile") == 0) {
                replacement = reinterpret_cast<void*>(&HookWriteFile);
                patchedWrite = true;
            } else if (
                std::strcmp(
                    reinterpret_cast<const char*>(import->Name), "ReadFile") == 0) {
                replacement = reinterpret_cast<void*>(&HookReadFile);
                patchedRead = true;
            } else if (
                std::strcmp(
                    reinterpret_cast<const char*>(import->Name),
                    "VirtualProtect") == 0) {
                replacement = reinterpret_cast<void*>(&HookVirtualProtect);
                patchedVirtualProtect = true;
            } else if (
                std::strcmp(
                    reinterpret_cast<const char*>(import->Name),
                    "VirtualAlloc") == 0) {
                replacement = reinterpret_cast<void*>(&HookVirtualAlloc);
                patchedVirtualAlloc = true;
            } else if (
                std::strcmp(
                    reinterpret_cast<const char*>(import->Name),
                    "Sleep") == 0) {
                replacement = reinterpret_cast<void*>(&HookSleep);
                patchedSleep = true;
            } else if (
                std::strcmp(
                    reinterpret_cast<const char*>(import->Name),
                    "GetTickCount") == 0) {
                replacement = reinterpret_cast<void*>(&HookGetTickCount);
                patchedGetTickCount = true;
            } else if (
                std::strcmp(
                    reinterpret_cast<const char*>(import->Name),
                    "QueryPerformanceCounter") == 0) {
                replacement =
                    reinterpret_cast<void*>(&HookQueryPerformanceCounter);
                patchedQueryPerformanceCounter = true;
            } else if (
                std::strcmp(
                    reinterpret_cast<const char*>(import->Name),
                    "DispatchMessageA") == 0) {
                replacement =
                    reinterpret_cast<void*>(&HookDispatchMessageA);
                patchedDispatchMessageA = true;
            }
            if (!replacement) {
                continue;
            }

            DWORD oldProtect = 0;
            if (!VirtualProtect(
                    &addresses->u1.Function,
                    sizeof(addresses->u1.Function),
                    PAGE_READWRITE,
                    &oldProtect)) {
                return false;
            }
            addresses->u1.Function =
                reinterpret_cast<ULONG_PTR>(replacement);
            DWORD ignored = 0;
            VirtualProtect(
                &addresses->u1.Function,
                sizeof(addresses->u1.Function),
                oldProtect,
                &ignored);
            FlushInstructionCache(
                GetCurrentProcess(),
                &addresses->u1.Function,
                sizeof(addresses->u1.Function));
        }
    }
    return patchedA && patchedW && patchedWrite && patchedRead &&
           patchedVirtualProtect && patchedVirtualAlloc && patchedSleep &&
           patchedGetTickCount && patchedQueryPerformanceCounter &&
           patchedDispatchMessageA;
}

}  // namespace

extern "C" __declspec(dllexport) DWORD WINAPI KoDPCNRefresh(void*) {
    static LONG refreshLogs = 0;
    if (InterlockedIncrement(&refreshLogs) <= 5) {
        WriteLog(L"external watchdog refresh");
    }
    EnsureRuntimeHooksFromGameThread();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }
    DisableThreadLibraryCalls(instance);

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    g_originalCreateFileA = reinterpret_cast<CreateFileAType>(
        GetProcAddress(kernel32, "CreateFileA"));
    g_originalCreateFileW = reinterpret_cast<CreateFileWType>(
        GetProcAddress(kernel32, "CreateFileW"));
    g_originalWriteFile = reinterpret_cast<WriteFileType>(
        GetProcAddress(kernel32, "WriteFile"));
    g_originalReadFile = reinterpret_cast<ReadFileType>(
        GetProcAddress(kernel32, "ReadFile"));
    g_originalVirtualProtect = reinterpret_cast<VirtualProtectType>(
        GetProcAddress(kernel32, "VirtualProtect"));
    g_originalVirtualAlloc = reinterpret_cast<VirtualAllocType>(
        GetProcAddress(kernel32, "VirtualAlloc"));
    g_originalMapViewOfFile = reinterpret_cast<MapViewOfFileType>(
        GetProcAddress(kernel32, "MapViewOfFile"));
    g_originalGetProcAddress = reinterpret_cast<GetProcAddressType>(
        GetProcAddress(kernel32, "GetProcAddress"));
    g_originalSleep = reinterpret_cast<SleepType>(
        GetProcAddress(kernel32, "Sleep"));
    g_originalGetTickCount = reinterpret_cast<GetTickCountType>(
        GetProcAddress(kernel32, "GetTickCount"));
    g_originalQueryPerformanceCounter =
        reinterpret_cast<QueryPerformanceCounterType>(
            GetProcAddress(kernel32, "QueryPerformanceCounter"));
    g_originalMultiByteToWideChar =
        reinterpret_cast<MultiByteToWideCharType>(
            GetProcAddress(kernel32, "MultiByteToWideChar"));
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    g_originalDrawTextA = reinterpret_cast<DrawTextAType>(
        GetProcAddress(user32, "DrawTextA"));
    g_originalDispatchMessageA = reinterpret_cast<DispatchMessageAType>(
        GetProcAddress(user32, "DispatchMessageA"));

    wchar_t dllPath[MAX_PATH] = {};
    GetModuleFileNameW(instance, dllPath, _countof(dllPath));
    wchar_t* slash = std::wcsrchr(dllPath, L'\\');
    if (slash) {
        *slash = L'\0';
    }
    wcsncpy_s(g_hookDirectory, dllPath, _TRUNCATE);

    if (!g_originalCreateFileA || !g_originalCreateFileW ||
        !g_originalWriteFile || !g_originalReadFile ||
        !g_originalVirtualProtect || !g_originalVirtualAlloc ||
        !g_originalMapViewOfFile || !g_originalGetProcAddress ||
        !g_originalSleep || !g_originalGetTickCount ||
        !g_originalQueryPerformanceCounter ||
        !g_originalDispatchMessageA) {
        return FALSE;
    }
    const bool patched = PatchMainExecutableImports();
    WriteLog(patched ? L"hook initialized" : L"failed to patch CreateFile imports");
    if (!patched) {
        return FALSE;
    }
    g_vectoredHandler =
        AddVectoredExceptionHandler(1, &HandleTextBreakpoint);
    if (!g_vectoredHandler) {
        WriteLog(L"failed to install breakpoint translation handler");
        return FALSE;
    }
    HANDLE thread = CreateThread(nullptr, 0, InstallGameHooks, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    }
    return thread ? TRUE : FALSE;
}
