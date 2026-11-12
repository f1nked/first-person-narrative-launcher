#include "../src/LauncherSettings.h"
#include <Windows.h>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace {
unsigned checks{};
bool ok=true;
void Check(bool pass,const char* label) { ++checks;if(!pass){ok=false;std::printf("FAIL: %s\n",label);} }
std::string Utf16(std::u16string_view text) {
    std::string bytes="\xff\xfe";
    for(const auto c:text){bytes+=static_cast<char>(c&255);bytes+=static_cast<char>(c>>8);}
    return bytes;
}
bool CameraValueIs(const std::string& text,const char* key,float expected) {
    const auto section=text.find("[Camera]\n");
    if(section==std::string::npos) return false;
    const auto token=std::string("\n")+key+"=";
    const auto at=text.find(token,section);
    if(at==std::string::npos) return false;
    const auto nextSection=text.find("\n[",section);
    if(nextSection!=std::string::npos && at>=nextSection) return false;
    char* end=nullptr;
    const auto* begin=text.c_str()+at+token.size();
    const float value=std::strtof(begin,&end);
    return end!=begin && std::isfinite(value) && std::fabs(value-expected)<0.0000006F;
}
void CheckCameraMotionRanges() {
    // Golden preset values cover endpoints, midpoint and an off-center percent.
    const struct {const char* key;float atZero,atFifty,atFiftyOne,atHundred;} values[]{
        {"HeadShakeXIntence",.020F,.060F,.061F,.100F},
        {"aim_HeadShakeXIntence",.020F,.020F,.020F,.020F},
        {"HeadShakeYIntence",.102F,.251F,.254F,.400F},
        {"aim_HeadShakeYIntence",.010F,.015F,.015F,.020F},
        {"HeadShakeZIntence",.102F,.251F,.254F,.400F},
        {"aim_HeadShakeZIntence",.010F,.015F,.015F,.020F},
        {"alan_wake_aim_horizontal_rotate",0,0,0,0},
        {"alan_wake_aim_vertical_rotate",.100F,0,-.002F,-.100F},
        {"tim_breaker_aim_horizontal_rotate",0,0,0,0},
        {"tim_breaker_aim_vertical_rotate",.100F,0,-.002F,-.100F},
        {"jesse_faden_aim_horizontal_rotate",0,0,0,0},
        {"jesse_faden_aim_vertical_rotate",.100F,0,-.002F,-.100F},
        {"janina_gavankar_aim_horizontal_rotate",0,0,0,0},
        {"janina_gavankar_aim_vertical_rotate",.100F,0,-.002F,-.100F}
    };
    const auto zero=launcher::UpdateSettings("[Camera]\n",{true,0});
    const auto fifty=launcher::UpdateSettings("[Camera]\n",{true,50});
    const auto fiftyOne=launcher::UpdateSettings("[Camera]\n",{true,51});
    const auto hundred=launcher::UpdateSettings("[Camera]\n",{true,100});
    for(const int percent:{0,51,100}) {
        const auto disabled=launcher::UpdateSettings("[Camera]\nhead_vertical_walk_stabilizer=0.000 ; manual\n",{true,percent});
        Check(disabled.find("head_vertical_walk_stabilizer=0.000 ; manual\n")!=std::string::npos,"CameraMotion preserves disabled vertical stabilizer");
        const auto custom=launcher::UpdateSettings("[Camera]\nhead_vertical_walk_stabilizer=0.735\n",{true,percent});
        Check(CameraValueIs(custom,"head_vertical_walk_stabilizer",.735F),"CameraMotion preserves custom vertical stabilizer");
    }
    Check(hundred.find("head_vertical_walk_stabilizer")==std::string::npos,"CameraMotion does not insert missing vertical stabilizer");
    const auto manualWide=launcher::UpdateSettings(Utf16(u"[Camera]\r\nhead_vertical_walk_stabilizer=0.000 ; manual\r\n"),{true,100});
    Check(manualWide.find(Utf16(u"head_vertical_walk_stabilizer=0.000 ; manual\r\n").substr(2))!=std::string::npos,"UTF16 CameraMotion preserves disabled stabilizer and inline comment");
    for(const auto& value:values) {
        Check(CameraValueIs(zero,value.key,value.atZero),value.key);
        Check(CameraValueIs(fifty,value.key,value.atFifty),value.key);
        Check(CameraValueIs(fiftyOne,value.key,value.atFiftyOne),value.key);
        Check(CameraValueIs(hundred,value.key,value.atHundred),value.key);
    }
    Check(launcher::UpdateSettings(fiftyOne,{true,51})==fiftyOne,"all motion values stable across repeated saves");
    Check(launcher::UpdateSettings(fiftyOne,{true,-1})==zero,"negative slider clamps to zero preset");
    Check(launcher::UpdateSettings(fiftyOne,{true,101})==hundred,"oversized slider clamps to hundred preset");
    const auto duplicates=launcher::UpdateSettings("[Camera]\njesse_faden_aim_vertical_rotate=9\njesse_faden_aim_vertical_rotate=8\n[Other]\njesse_faden_aim_vertical_rotate=7\n",{true,51});
    Check(duplicates.find("jesse_faden_aim_vertical_rotate=-0.002\njesse_faden_aim_vertical_rotate=-0.002")!=std::string::npos &&
        duplicates.find("[Other]\njesse_faden_aim_vertical_rotate=7")!=std::string::npos,"preset updates duplicates only in Camera section");
    const auto wide=launcher::UpdateSettings(Utf16(u"[Camera]\r\n; keep Unicode\r\n"),{true,51});
    Check(wide.find(Utf16(u"jesse_faden_aim_vertical_rotate=-0.002\r\n").substr(2))!=std::string::npos,"UTF16 persistence retains intermediate aim values");
}
}
int main() {
    using namespace launcher;
    CheckCameraMotionRanges();
    const std::string original="\xef\xbb\xbf[Camera]\r\n; custom comment\r\nEnableFirstPersonNarrative = 1 ; keep comment\r\nWriteLog=0\r\ncustom=preserve\r\n\r\n[Other]\r\nEnabled=23\r\n";
    const auto updated=UpdateSettings(original,{false,73});
    Check(updated.starts_with("\xef\xbb\xbf[Camera]\r\n; custom comment\r\nEnableFirstPersonNarrative = 0 ; keep comment\r\nWriteLog=0\r\ncustom=preserve\r\n") &&
        updated.find("[Other]\r\nEnabled=23\r\n")!=std::string::npos,"UTF8 BOM, CRLF, comments, unrelated keys and sections preserved byte-for-byte");
    Check(!ReadSettings(updated).enabled&&ReadSettings(updated).sensitive==73,"updated values roundtrip");
    Check(UpdateSettings(updated,{false,73})==updated,"repeated save is idempotent");
    Check(ReadSettings("").enabled&&ReadSettings("").sensitive==50,"missing preferences default on and 50 percent");
    Check(ReadSettings("[Camera]\nCameraMotion=oops\n").sensitive==50,"invalid sensitivity falls back to default");
    Check(ReadSettings("[Camera]\nCameraMotion=200\n").sensitive==100,"sensitivity cannot exceed 100");
    const auto lf=UpdateSettings("[Other]\nkeep=42",{true,0});
    Check(lf.starts_with("[Other]\nkeep=42\n[Camera]\nEnableFirstPersonNarrative=1\nCameraMotion=0\n") &&
        lf.back()=='\n' && CameraValueIs(lf,"alan_wake_aim_vertical_rotate",.100F),"missing sections and missing final newline handled");
    const auto middle=UpdateSettings("[Camera]\nkeep=42\n[Other]\nx=3\n",{false,100});
    Check(middle.find("keep=42\nEnableFirstPersonNarrative=0\nCameraMotion=100\n")!=std::string::npos &&
        middle.find("[Other]\nx=3\n")!=std::string::npos,"missing key inserted in correct section");
    const auto duplicates=UpdateSettings("[camera]\nenablefirstpersonnarrative=1\nEnableFirstPersonNarrative=1\n",{false,50});
    Check(duplicates.find("narrative=0")!=std::string::npos&&duplicates.find("Narrative=0")!=std::string::npos,"case insensitive keys and duplicate flags updated consistently");
    const auto utf16=Utf16(u"[Camera]\r\nEnableFirstPersonNarrative=1\r\n; тест\r\n[Launcher]\r\nSensitive=50\r\n");
    const auto changed16=UpdateSettings(utf16,{false,99});
    Check(changed16.starts_with(Utf16(u"[Camera]\r\nEnableFirstPersonNarrative=0\r\n; тест\r\n")) &&
        changed16.find(Utf16(u"[Launcher]\r\nSensitive=50\r\n").substr(2))!=std::string::npos,"UTF16 LE BOM, Unicode comments and unrelated legacy section preserved");
    Check(!ReadSettings(changed16).enabled&&ReadSettings(changed16).sensitive==99,"UTF16 settings roundtrip");
    auto bindings=ReadSettings("[Camera]\ntoggleKeyboardButton=BACKSPACE\n[Input]\nturnback_right_btn=MOUSE_FORWARD\nturnback_left_btn=MOUSE_BACK\n");
    Check(bindings.switchCamera=="BACKSPACE"&&bindings.tiltRight=="MOUSE_FORWARD"&&bindings.tiltLeft=="MOUSE_BACK","read current native switch and tilt bindings");
    bindings.switchCamera="P";bindings.tiltRight="E";bindings.tiltLeft="Q";
    const auto rebound=UpdateSettings(original,bindings);
    const auto reboundSettings=ReadSettings(rebound);
    Check(reboundSettings.switchCamera=="P"&&reboundSettings.tiltRight=="E"&&reboundSettings.tiltLeft=="Q"&&
        rebound.find("custom=preserve")!=std::string::npos,"binding edits save correct sections without removing other mod keys");
    Check(ReadSettings("[Camera]\ntuggleKeyboardButton=F8\n").switchCamera=="F8","read legacy switch binding as fallback");
    Check(ReadSettings("").switchCamera=="F6"&&ReadSettings("").tiltRight=="MOUSE_FORWARD"&&ReadSettings("").tiltLeft=="MOUSE_BACK","missing bindings match native mod defaults");
    const auto rebound16=ReadSettings(UpdateSettings(utf16,bindings));
    Check(rebound16.switchCamera=="P"&&rebound16.tiltRight=="E"&&rebound16.tiltLeft=="Q","binding edits preserve UTF16 encoding");
    auto unicodeBindings=bindings;unicodeBindings.switchCamera="Ъ";
    Check(ReadSettings(UpdateSettings(utf16,unicodeBindings)).switchCamera=="Ъ","UTF16 keyboard aliases roundtrip as UTF8 without corruption");
    const auto directory=std::filesystem::temp_directory_path()/(L"AW2-Launcher-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directory(directory);const auto path=directory/L"FirstPersonMod.ini";
    std::wstring error;std::string read;
    const std::string defaults="[Camera]\r\nEnableFirstPersonNarrative=1\r\nPreset=DEFAULT\r\n";
    const std::string versionedDefaults="[Camera]\r\nversion=1.0.1\r\nPreset=DEFAULT\r\n";
    const std::string versionedCustom="[Camera]\r\nversion=1.0.1 ; current\r\nPreset=CUSTOM\r\n";
    Check(WriteFileAtomic(path,versionedCustom,error),"create current version settings");
    const auto beforeTime=std::filesystem::last_write_time(path);
    Check(LoadCurrentSettings(path,versionedDefaults,read,error)&&read==versionedCustom&&
        std::filesystem::last_write_time(path)==beforeTime,"matching version retains exact file without writing");
    Check(WriteFileAtomic(path,"[Camera]\nversion=1.0.0\nPreset=CUSTOM\n",error)&&
        LoadCurrentSettings(path,versionedDefaults,read,error)&&read==versionedDefaults&&
        ReadFile(path,read,error)&&read==versionedDefaults,"old version replaced with bundled defaults");
    Check(WriteFileAtomic(path,"[Camera]\nPreset=CUSTOM\n",error)&&
        LoadCurrentSettings(path,versionedDefaults,read,error)&&read==versionedDefaults,"missing version replaced with bundled defaults");
    const auto versionedWide=Utf16(u"[Camera]\r\nversion=1.0.1\r\nPreset=CUSTOM\r\n");
    Check(WriteFileAtomic(path,versionedWide,error)&&LoadCurrentSettings(path,versionedDefaults,read,error)&&
        read==versionedWide,"matching UTF16 version retains custom settings");
    Check(!LoadCurrentSettings(path,defaults,read,error)&&ReadFile(path,read,error)&&read==versionedWide,
        "invalid bundled version cannot replace existing settings");
    std::filesystem::remove(path);
    Check(LoadCurrentSettings(path,versionedDefaults,read,error)&&read==versionedDefaults&&
        ReadFile(path,read,error)&&read==versionedDefaults,"missing settings created with versioned defaults");
    Check(WriteFileAtomic(path,original,error),"write real private INI atomically");
    Check(SaveSettings(path,{false,73},defaults,false,error)&&ReadFile(path,read,error)&&read==updated,"save reads current real file and preserves other settings");
    read+="; external edit\r\n";Check(WriteFileAtomic(path,read,error),"simulate another editor changing INI while UI is open");
    Check(SaveSettings(path,{true,74},defaults,false,error)&&ReadFile(path,read,error)&&read.find("; external edit")!=std::string::npos,"saving preferences preserves latest external edits");
    Check(SaveSettings(path,{true,50},defaults,true,error)&&ReadFile(path,read,error)&&read==UpdateSettings(defaults,{true,50}),"full default reset restores all bundled INI settings");
    auto backup=path;backup+=L".launcher-backup";
    Check(!std::filesystem::exists(backup),"full reset does not create a launcher backup");
    std::filesystem::remove(path);
    Check(SaveSettings(path,{true,50},defaults,false,error)&&ReadFile(path,read,error)&&read==UpdateSettings(defaults,{true,50}),"missing INI is created from bundled defaults");
    Check(!SaveSettings(directory/L"missing"/L"FirstPersonMod.ini",{true,50},defaults,false,error),"invalid directory reports failure");
    for(const auto& entry:std::filesystem::directory_iterator(directory))std::filesystem::remove(entry.path());
    std::filesystem::remove(directory);
    std::printf("Launcher settings checks=%u result=%s\n",checks,ok?"PASS":"FAIL");
    return ok?0:1;
}
