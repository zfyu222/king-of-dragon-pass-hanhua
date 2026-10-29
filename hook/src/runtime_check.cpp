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
    const size_t feedbackStart = probes.size();
    probes.push_back("Leikan, age 49\nAnimals: Good\nBargaining: Good\nCombat: Excellent\nCustom: Excellent\nLeadership: Excellent\nMagic: Very Good\nWorships: Orlanth");
    probes.push_back("The Issaries priestess Sora Goodseller came to our clan and asked if we had any white horses to trade. We had no white horses. Embarrassed that we had no white horse, we gave her a feast instead.");
    probes.push_back("Eonislara, betrothed to Jorator of the Blackrock clan, was seduced by Korol, the notorious troublemaking poet of the Heran clan.");
    probes.push_back("To prepare for the marriage of Ernalda and Orlanth, your men and women stood guard with Elmal.");
    probes.push_back("We managed to sneak around their patrols and spirit away 22 cows without being spotted.");
    probes.push_back("Farmer Morale +\nWarrior Morale +");
    probes.push_back("We had to maneuver under a deadly rain of missiles. It was a short, nasty battle. Their magic confused and confounded our warriors. We were driven off and had no opportunity to obtain booty. With their careful attention, our auxiliaries kept 1 warrior from dying, and healed 1 of the wounded.");
    probes.push_back(u8"Weaponthanes阵亡\nFootmen阵亡");
    probes.push_back("We had to maneuver under a devastating barrage of missiles.");
    probes.push_back("With their careful attention, our auxiliaries kept 3 warriors from dying, and healed 12 of the wounded.");
    int failed = 0;
    size_t probeIndex = 0;
    for (const char* probe : probes) {
        std::string result = TranslateRuntimeText(probe);
        bool changed = result != probe && result.find(u8"兽皮") == std::string::npos;
        if (probeIndex++ >= feedbackStart && std::any_of(result.begin(), result.end(), [](unsigned char c) { return c < 128 && std::isalpha(c); })) changed = false;
        std::cout << (changed ? "PASS " : "FAIL ") << probe << "\n" << result << "\n";
        if (!changed) ++failed;
    }
    const char* unknown = "UncataloguedQuux 123";
    if (std::string(TranslateRuntimeText(unknown)) != unknown) ++failed;
    if (std::string(TranslateRuntimeText("Magic: Very Good")).find("Very Good") != std::string::npos) ++failed;
    const std::string counts = TranslateRuntimeText("With their careful attention, our auxiliaries kept 3 warriors from dying, and healed 12 of the wounded.");
    if (counts.find("3") == std::string::npos || counts.find("12") == std::string::npos) ++failed;
    if (std::string(TranslateRuntimeText("Magic: dead")) != "Magic: dead") ++failed;
    const std::string boundary(33000, '+');
    if (std::string(TranslateRuntimeText(boundary.c_str())) != boundary) ++failed;
    return failed ? 1 : 0;
}
