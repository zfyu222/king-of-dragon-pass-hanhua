#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

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

CreateFileAType g_originalCreateFileA = nullptr;
CreateFileWType g_originalCreateFileW = nullptr;
WriteFileType g_originalWriteFile = nullptr;
ReadFileType g_originalReadFile = nullptr;
VirtualProtectType g_originalVirtualProtect = nullptr;
VirtualAllocType g_originalVirtualAlloc = nullptr;
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
struct RuntimeAllocation {
    unsigned char* address;
    size_t bytes;
};
RuntimeAllocation g_runtimeAllocations[64] = {};
LONG g_runtimeAllocationCount = 0;
std::unordered_map<std::string, std::string> g_translations;

struct FormatPattern {
    std::vector<std::string> literals;
    std::vector<std::string> tokens;
    std::string translated;
};
std::vector<FormatPattern> g_formatPatterns;

void WriteLog(const wchar_t* message);

using AdvisorTextType = int*(__thiscall*)(void*, const char*, int);
using CaptionType = int(__thiscall*)(void*, const char*);
using TextBuilderType = void*(__thiscall*)(void*, const char*);
using ResolveResourceType = int(__cdecl*)(int*, const char*, int);
AdvisorTextType g_originalAdvisorText = nullptr;
CaptionType g_originalCaption = nullptr;
TextBuilderType g_originalBuilderStart = nullptr;
TextBuilderType g_originalBuilderAppend = nullptr;
ResolveResourceType g_originalResolveResource = nullptr;
void* g_originalFontFinalizeTail = nullptr;
unsigned char* g_runtimeImageBase = nullptr;
LONG g_fontOverrideLogCount = 0;

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
    int written = -1;
    if (EndsWith(normalized, L"kodp\\scenes.dat")) {
        written = _snwprintf_s(
            overlayPath,
            overlayCapacity,
            _TRUNCATE,
            L"%s\\overlay\\KoDp\\scenes.dat",
            g_hookDirectory);
    } else {
        return false;
    }
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

bool LoadTranslations() {
    const std::wstring databasePath =
        std::wstring(g_hookDirectory) + L"\\translations.tsv";
    std::ifstream input(databasePath, std::ios::binary);
    if (!input) {
        WriteLog(L"translations.tsv was not found");
        return false;
    }
    std::string line;
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
        g_translations.emplace(source, translated);
        FormatPattern pattern;
        if (ParseFormatPattern(source, translated, &pattern)) {
            g_formatPatterns.push_back(std::move(pattern));
        }
    }
    wchar_t message[256] = {};
    _snwprintf_s(
        message,
        _countof(message),
        _TRUNCATE,
        L"loaded %u exact translations and %u format patterns",
        static_cast<unsigned>(g_translations.size()),
        static_cast<unsigned>(g_formatPatterns.size()));
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

const char* TranslateRuntimeText(const char* source) {
    if (!source || !*source) {
        return source;
    }
    const auto exact = g_translations.find(source);
    if (exact != g_translations.end()) {
        return exact->second.c_str();
    }

    thread_local std::string formatted;
    std::vector<std::string> captures;
    const std::string text(source);
    for (const FormatPattern& pattern : g_formatPatterns) {
        if (!MatchFormat(pattern, text, &captures)) {
            continue;
        }
        formatted = RenderFormat(pattern.translated, captures);
        if (!formatted.empty()) {
            return formatted.c_str();
        }
    }
    return source;
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

void __stdcall PrepareExternalFont(void* font) {
    if (!font || !g_runtimeImageBase ||
        *reinterpret_cast<void**>(
            static_cast<unsigned char*>(font) + 48) != nullptr) {
        return;
    }

    static const char* candidates[] = {
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\Deng.ttf",
        "C:\\Windows\\Fonts\\simsunb.ttf",
    };
    const char* fontPath = nullptr;
    for (const char* candidate : candidates) {
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            fontPath = candidate;
            break;
        }
    }
    if (!fontPath) {
        if (InterlockedCompareExchange(&g_fontOverrideLogCount, 1, 0) == 0) {
            WriteLog(L"no supported system Chinese TrueType font was found");
        }
        return;
    }

    using CreateTTDescriptorType = void*(__cdecl*)();
    using GameAllocateType = void*(__cdecl*)(size_t);
    auto createDescriptor = reinterpret_cast<CreateTTDescriptorType>(
        g_runtimeImageBase + 0x7089C);
    auto gameAllocate = reinterpret_cast<GameAllocateType>(
        g_runtimeImageBase + 0xB77E0);
    auto* descriptor =
        static_cast<unsigned char*>(createDescriptor());
    const size_t pathBytes = std::strlen(fontPath) + 1;
    auto* storedPath =
        static_cast<char*>(gameAllocate(pathBytes));
    if (!descriptor || !storedPath) {
        WriteLog(L"failed to allocate external TrueType font descriptor");
        return;
    }
    std::memcpy(storedPath, fontPath, pathBytes);
    *reinterpret_cast<char**>(descriptor + 16) = storedPath;
    *reinterpret_cast<void**>(descriptor + 20) = nullptr;
    *reinterpret_cast<unsigned int*>(descriptor + 24) = 0;
    *reinterpret_cast<void**>(
        static_cast<unsigned char*>(font) + 48) = descriptor;

    const LONG count = InterlockedIncrement(&g_fontOverrideLogCount);
    if (count <= 20) {
        wchar_t widePath[MAX_PATH] = {};
        MultiByteToWideChar(
            CP_ACP, 0, fontPath, -1, widePath, _countof(widePath));
        wchar_t message[MAX_PATH + 80] = {};
        _snwprintf_s(
            message,
            _countof(message),
            _TRUNCATE,
            L"attached external TrueType font: %s",
            widePath);
        WriteLog(message);
    }
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
    const char* translated = TranslateRuntimeText(source);
    LogTranslationHit(source, translated);
    return g_originalAdvisorText(self, translated, advisorIndex);
}

int __fastcall HookCaption(void* self, void*, const char* source) {
    const char* translated = TranslateRuntimeText(source);
    LogTranslationHit(source, translated);
    return g_originalCaption(self, translated);
}

void* __fastcall HookBuilderStart(void* self, void*, const char* source) {
    const char* translated = TranslateRuntimeText(source);
    LogTranslationHit(source, translated);
    return g_originalBuilderStart(self, translated);
}

void* __fastcall HookBuilderAppend(void* self, void*, const char* source) {
    const char* translated = TranslateRuntimeText(source);
    LogTranslationHit(source, translated);
    return g_originalBuilderAppend(self, translated);
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
        VirtualFree(trampoline, 0, MEM_RELEASE);
        wchar_t message[200] = {};
        _snwprintf_s(
            message,
            _countof(message),
            _TRUNCATE,
            L"VirtualProtect failed for hook target 0x%08X: error=%u",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(target)),
            error);
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
    if (!IsReadableCode(base + 0x10B470, sizeof(advisorPattern)) ||
        !IsReadableCode(base + 0x161430, sizeof(builderStartPattern)) ||
        !IsReadableCode(base + 0x161550, sizeof(builderAppendPattern)) ||
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
    g_originalCaption = reinterpret_cast<CaptionType>(
        InstallInlineHook(base + 0x92E7A, &HookCaption, 7));
    g_originalAdvisorText = reinterpret_cast<AdvisorTextType>(
        InstallInlineHook(base + 0x10B470, &HookAdvisorText, 5));
    g_originalBuilderStart = reinterpret_cast<TextBuilderType>(
        InstallInlineHook(base + 0x161430, &HookBuilderStart, 5));
    g_originalBuilderAppend = reinterpret_cast<TextBuilderType>(
        InstallInlineHook(base + 0x161550, &HookBuilderAppend, 6));
    const bool installed =
        g_originalCaption && g_originalAdvisorText &&
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
        if (InterlockedCompareExchange(&g_translationsReady, 0, 0) != 0) {
            auto** argument =
                reinterpret_cast<const char**>(context->Esp + sizeof(DWORD));
            const char* source = *argument;
            const char* translated = TranslateRuntimeText(source);
            *argument = translated;
            LogTranslationHit(source, translated);
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
    if (InterlockedCompareExchange(&g_translationsReady, 0, 0) != 0) {
        auto** argument =
            reinterpret_cast<const char**>(context->Esp + sizeof(DWORD));
        const char* source = *argument;
        const char* translated = TranslateRuntimeText(source);
        *argument = translated;
        LogTranslationHit(source, translated);
    }

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

    SYSTEM_INFO systemInfo = {};
    GetSystemInfo(&systemInfo);
    auto* cursor =
        static_cast<unsigned char*>(systemInfo.lpMinimumApplicationAddress);
    auto* maximum =
        static_cast<unsigned char*>(systemInfo.lpMaximumApplicationAddress);
    while (cursor < maximum) {
        MEMORY_BASIC_INFORMATION memory = {};
        if (VirtualQuery(cursor, &memory, sizeof(memory)) != sizeof(memory)) {
            break;
        }
        const DWORD executeMask =
            PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
            PAGE_EXECUTE_WRITECOPY;
        const bool searchable =
            memory.State == MEM_COMMIT &&
            (memory.Protect & executeMask) != 0 &&
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
        if (searchable && memory.RegionSize >= sizeof(captionPattern)) {
            auto* region = static_cast<unsigned char*>(memory.BaseAddress);
            const size_t limit =
                memory.RegionSize - sizeof(captionPattern) + 1;
            for (size_t offset = 0; offset < limit; ++offset) {
                unsigned char* hit = region + offset;
                if (!MatchMasked(hit, captionPattern, captionMask) ||
                    reinterpret_cast<uintptr_t>(hit) < 0x92E7A) {
                    continue;
                }
                unsigned char* base = hit - 0x92E7A;
                if (MatchMasked(
                        base + 0x10B470, advisorPattern, advisorMask) &&
                    MatchMasked(
                        base + 0x161430,
                        builderStartPattern,
                        builderStartMask) &&
                    MatchMasked(
                        base + 0x161550,
                        builderAppendPattern,
                        builderAppendMask)) {
                    return base;
                }
            }
        }
        cursor += memory.RegionSize;
    }
    return nullptr;
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

DWORD WINAPI InstallGameHooks(void*) {
    if (!LoadTranslations()) {
        return 1;
    }
    InterlockedExchange(&g_translationsReady, 1);
    WriteLog(L"breakpoint translation handler ready");
    unsigned char* base = nullptr;
    for (int attempt = 0; attempt < 120 && !base; ++attempt) {
        base = FindRuntimeImageBase();
        if (!base) {
            Sleep(250);
        }
    }
    if (!base) {
        WriteLog(L"could not locate the executable s86 image for hardware breakpoints");
        return 2;
    }
    g_hardwareTargets[0] =
        reinterpret_cast<uintptr_t>(base + 0x92E7A);
    g_hardwareTargets[1] =
        reinterpret_cast<uintptr_t>(base + 0x10B470);
    g_hardwareTargets[2] =
        reinterpret_cast<uintptr_t>(base + 0x161430);
    g_hardwareTargets[3] =
        reinterpret_cast<uintptr_t>(base + 0x161550);
    InterlockedExchange(&g_hardwareBreakpointsReady, 1);
    const int applied = ApplyHardwareBreakpointsToGameThreads();
    wchar_t message[200] = {};
    _snwprintf_s(
        message,
        _countof(message),
        _TRUNCATE,
        L"installed hardware text breakpoints at s86 base 0x%08X on %d threads",
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(base)),
        applied);
    WriteLog(message);
    for (int attempt = 0; attempt < 120; ++attempt) {
        Sleep(500);
        ApplyHardwareBreakpointsToGameThreads();
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
        if (InterlockedCompareExchange(&g_redirectLogWritten, 1, 0) == 0) {
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
            if (InterlockedCompareExchange(&g_redirectLogWritten, 1, 0) == 0) {
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
           patchedVirtualProtect && patchedVirtualAlloc;
}

}  // namespace

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

    wchar_t dllPath[MAX_PATH] = {};
    GetModuleFileNameW(instance, dllPath, _countof(dllPath));
    wchar_t* slash = std::wcsrchr(dllPath, L'\\');
    if (slash) {
        *slash = L'\0';
    }
    wcsncpy_s(g_hookDirectory, dllPath, _TRUNCATE);

    if (!g_originalCreateFileA || !g_originalCreateFileW ||
        !g_originalWriteFile || !g_originalReadFile ||
        !g_originalVirtualProtect || !g_originalVirtualAlloc) {
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
