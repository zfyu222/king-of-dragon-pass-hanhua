// Exercise the real lookup implementation without injecting or opening a game.
#include "hook.cpp"
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    wcsncpy_s(g_hookDirectory, argv[1], _TRUNCATE);
    g_originalCreateFileW = &CreateFileW;
    if (!LoadTranslations()) return 3;
    #include "../build/native/runtime_probes.inc"
    const std::string partial = std::string(
        "They're known for keeping close ties with their former kin in\n"
        "Heortland. Their chief is Elusu. They're in the Colymar tribe. ") +
        TranslateRuntimeText("They owe us a favor.");
    probes.push_back(partial.c_str());
    int failed = 0;
    for (const char* probe : probes) {
        std::string result = TranslateRuntimeText(probe);
        bool changed = result != probe && result.find(u8"兽皮") == std::string::npos;
        std::cout << (changed ? "PASS " : "FAIL ") << probe << "\n" << result << "\n";
        if (!changed) ++failed;
    }
    return failed ? 1 : 0;
}
