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
    probes.push_back("We stole 22 cows from the Greenwings.");
    probes.push_back("The warriors refused to raid the Hiordlings during Sea season.");
    probes.push_back(u8"The Gorinds charged right into the trap we had laid for them. The battle didn’t last long. Their magic was stronger than ours, but we were not dismayed. We drove the Gorinds from the battlefield, and were able to plunder their tula. We captured 23 cows and 1 horse. With their careful attention, our auxiliaries kept 2 warriors from dying, and bandaged 4 of the wounded.");
    probes.push_back(u8"The Gorinds rushed us before we’d fired more than a few volleys. The battle didn’t last long. Their magic confused and confounded our warriors. We were driven off and had no opportunity to obtain booty. Due to their prompt healing, our auxiliaries kept 1 warrior from dying, and patched up 3 of the wounded.");
    probes.push_back("The Horse Nomad charge hit us before we could shift our lines.");
    probes.push_back("As our forces engage with the Horse Nomads, Orlaront notices that a detachment of riders is trying to sneak completely around and get at us from behind. What should he do?");
    probes.push_back("Make sure Varsens knows about the threat.");
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
    const std::string articleResult=TranslateRuntimeText("The Horse Nomad charge hit us before we could shift our lines.");
    if(articleResult.rfind(u8"这",0)==0) ++failed;
    const std::string battle = TranslateRuntimeText(probes[feedbackStart+12]);
    for(const char* required : {"23","1","2","4",u8"辅助",u8"包扎"})
        if(battle.find(required)==std::string::npos) {std::cout<<"FAIL battle lost "<<required<<"\n";++failed;}
    const std::string retreat=TranslateRuntimeText(probes[feedbackStart+13]);
    for(const char* required : {"1","3",u8"战利品",u8"救治"})
        if(retreat.find(required)==std::string::npos) ++failed;
    if(RenderFormat("%s / %d",{"name","12"})!="name / 12") ++failed;
    if(RenderFormat("%s%d",{"name","12"})!="name12") ++failed;
    FormatPattern possessive;
    const std::string genderSetter("\x01\xBA\x0B",3), pronoun(1,4);
    if(!BuildOpalRuntimePattern("Members of "+genderSetter+pronoun+" own clan.",u8"成员来自"+genderSetter+pronoun+u8"自己的氏族。",&possessive)
        ||RenderPattern(possessive,{u8"她"})!=u8"成员来自她自己的氏族。") ++failed;
    FormatPattern ordered;
    const std::string firstMarker("\x01@\x14",3), secondMarker("\x01\xD8\0",3);
    if(!BuildOpalRuntimePattern(firstMarker+" charged past "+secondMarker+".", secondMarker+" / "+firstMarker, &ordered)
        || RenderPattern(ordered,{"A","B"})!="B / A") ++failed;
    return failed ? 1 : 0;
}
