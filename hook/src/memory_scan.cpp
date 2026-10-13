#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::fwprintf(
            stderr,
            L"usage: memory_scan.exe <pid> <ascii needle>\n"
            L"       memory_scan.exe <pid> --dump <address> <bytes>\n"
            L"       memory_scan.exe <pid> --write <address> <hex bytes>\n");
        return 1;
    }
    const DWORD processId = std::wcstoul(argv[1], nullptr, 10);
    if (std::wcscmp(argv[2], L"--dump") == 0 ||
        std::wcscmp(argv[2], L"--write") == 0) {
        if (argc < 5) {
            return 1;
        }
        const uintptr_t address =
            static_cast<uintptr_t>(std::wcstoull(argv[3], nullptr, 0));
        const bool write = std::wcscmp(argv[2], L"--write") == 0;
        HANDLE process = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                (write ? PROCESS_VM_WRITE | PROCESS_VM_OPERATION : 0),
            FALSE,
            processId);
        if (!process) {
            std::fwprintf(stderr, L"OpenProcess failed: %lu\n", GetLastError());
            return 3;
        }
        if (write) {
            std::vector<unsigned char> bytes;
            const wchar_t* cursor = argv[4];
            while (*cursor) {
                while (*cursor == L' ') {
                    ++cursor;
                }
                if (!*cursor) {
                    break;
                }
                wchar_t* end = nullptr;
                const unsigned long value = std::wcstoul(cursor, &end, 16);
                if (end == cursor || value > 0xFF) {
                    CloseHandle(process);
                    return 2;
                }
                bytes.push_back(static_cast<unsigned char>(value));
                cursor = end;
            }
            SIZE_T written = 0;
            const BOOL ok = WriteProcessMemory(
                process,
                reinterpret_cast<void*>(address),
                bytes.data(),
                bytes.size(),
                &written);
            std::printf(
                "write address=0x%08lX requested=%zu written=%zu ok=%d error=%lu\n",
                static_cast<unsigned long>(address),
                bytes.size(),
                static_cast<size_t>(written),
                ok,
                ok ? 0 : GetLastError());
            CloseHandle(process);
            return ok && written == bytes.size() ? 0 : 4;
        }
        const size_t count = std::wcstoul(argv[4], nullptr, 0);
        std::vector<unsigned char> bytes(count);
        SIZE_T read = 0;
        if (!ReadProcessMemory(
                process,
                reinterpret_cast<void*>(address),
                bytes.data(),
                bytes.size(),
                &read)) {
            std::printf("ReadProcessMemory failed: %lu\n", GetLastError());
            CloseHandle(process);
            return 4;
        }
        for (size_t offset = 0; offset < read; offset += 16) {
            std::printf(
                "%08lX  ",
                static_cast<unsigned long>(address + offset));
            for (size_t column = 0; column < 16; ++column) {
                if (offset + column < read) {
                    std::printf("%02X ", bytes[offset + column]);
                } else {
                    std::printf("   ");
                }
            }
            std::printf(" ");
            for (size_t column = 0; column < 16 && offset + column < read;
                 ++column) {
                const unsigned char value = bytes[offset + column];
                std::putchar(value >= 32 && value <= 126 ? value : '.');
            }
            std::putchar('\n');
        }
        CloseHandle(process);
        return 0;
    }
    std::vector<unsigned char> needle;
    const bool hex = std::wcsncmp(argv[2], L"hex:", 4) == 0;
    const bool wide = !hex && argc >= 4 && std::wcscmp(argv[3], L"--wide") == 0;
    if (hex) {
        const wchar_t* cursor = argv[2] + 4;
        while (*cursor) {
            while (*cursor == L' ') {
                ++cursor;
            }
            if (!*cursor) {
                break;
            }
            wchar_t* end = nullptr;
            const unsigned long value = std::wcstoul(cursor, &end, 16);
            if (end == cursor || value > 0xFF) {
                return 2;
            }
            needle.push_back(static_cast<unsigned char>(value));
            cursor = end;
        }
    } else if (wide) {
        const size_t chars = std::wcslen(argv[2]);
        needle.resize(chars * sizeof(wchar_t));
        std::memcpy(needle.data(), argv[2], needle.size());
    } else {
        const int needed =
            WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, nullptr, 0, nullptr, nullptr);
        if (needed <= 1) {
            return 2;
        }
        needle.resize(static_cast<size_t>(needed - 1));
        WideCharToMultiByte(
            CP_UTF8,
            0,
            argv[2],
            -1,
            reinterpret_cast<char*>(needle.data()),
            needed,
            nullptr,
            nullptr);
    }

    HANDLE process = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (!process) {
        std::fwprintf(stderr, L"OpenProcess failed: %lu\n", GetLastError());
        return 3;
    }

    SYSTEM_INFO system = {};
    GetSystemInfo(&system);
    auto* cursor =
        static_cast<unsigned char*>(system.lpMinimumApplicationAddress);
    auto* maximum =
        static_cast<unsigned char*>(system.lpMaximumApplicationAddress);
    size_t matches = 0;
    while (cursor < maximum) {
        MEMORY_BASIC_INFORMATION memory = {};
        if (VirtualQueryEx(process, cursor, &memory, sizeof(memory)) !=
            sizeof(memory)) {
            break;
        }
        const DWORD blocked = PAGE_GUARD | PAGE_NOACCESS;
        if (memory.State == MEM_COMMIT &&
            (memory.Protect & blocked) == 0 &&
            memory.RegionSize <= 512ull * 1024 * 1024) {
            std::vector<unsigned char> bytes(memory.RegionSize);
            SIZE_T read = 0;
            if (ReadProcessMemory(
                    process, memory.BaseAddress, bytes.data(), bytes.size(), &read)) {
                for (size_t offset = 0;
                     offset + needle.size() <= read;
                     ++offset) {
                    if (std::memcmp(
                            bytes.data() + offset,
                            needle.data(),
                            needle.size()) == 0) {
                        std::printf(
                            "match=0x%08lX region=0x%08lX size=0x%zX "
                            "protect=0x%lX type=0x%lX alloc=0x%08lX\n",
                            static_cast<unsigned long>(
                                reinterpret_cast<uintptr_t>(memory.BaseAddress) +
                                offset),
                            static_cast<unsigned long>(
                                reinterpret_cast<uintptr_t>(memory.BaseAddress)),
                            memory.RegionSize,
                            memory.Protect,
                            memory.Type,
                            static_cast<unsigned long>(
                                reinterpret_cast<uintptr_t>(memory.AllocationBase)));
                        ++matches;
                    }
                }
            }
        }
        auto* next =
            static_cast<unsigned char*>(memory.BaseAddress) + memory.RegionSize;
        if (next <= cursor) {
            break;
        }
        cursor = next;
    }
    CloseHandle(process);
    std::printf("matches=%zu\n", matches);
    return 0;
}
