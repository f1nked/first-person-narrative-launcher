#include <Windows.h>
#include <Windowsx.h>
#include <CommCtrl.h>
#include <TlHelp32.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include "LauncherSettings.h"
#include "ConfigPath.h"

namespace {
constexpr int kWidth=712,kHeight=612;
constexpr int kClose=10,kEnable=11,kSlider=12,kReset=13,kApply=14,kLaunch=15;
constexpr int kPatreon=16,kSwitch=17,kRight=18,kLeft=19;
constexpr COLORREF kBackground=RGB(25,26,27),kText=RGB(213,225,215),kAccent=RGB(162,182,170);
struct HoverState {float progress=0;};
HoverState elementHover[20],captureFade[3];
ULONGLONG animationTick=0;
struct App {
    HWND window{},close{},enable{},slider{},apply{},reset{},launch{},patreon{},bindings[3]{};
    HFONT font{};
    HBRUSH background{};
    UINT dpi=96;
    launcher::Settings settings;
    launcher::Settings initialSettings;
    bool configReady=false;
    bool resetAll=false,failed=false;
    bool gameFound=false;
    bool gameRunning=false;
    ULONGLONG gameRunningCheckTick=0;
    int capturing=0;
    int sliderPreview=50;
    float sliderDisplayed=50.0F,sliderAnimationFrom=50.0F;
    ULONGLONG sliderAnimationTick=0;
    bool sliderDragging=false,sliderKeyboardEditing=false;
    std::wstring status;
    std::filesystem::path directory,ini,game;
    std::string defaults;
    std::unique_ptr<Gdiplus::Bitmap> artwork;
    std::unique_ptr<Gdiplus::PrivateFontCollection> uiFonts;
    std::unique_ptr<Gdiplus::PrivateFontCollection> statusFonts;
    std::unique_ptr<Gdiplus::Font> statusFont;
    IStream* artworkStream{};

    int S(int value) const {return MulDiv(value,static_cast<int>(dpi),96);}
} app;

// Finish each frame offscreen before presenting it to the window.
struct PaintBuffer {
    HDC target{},dc{};
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    RECT area{};
    PaintBuffer(HDC destination,const RECT& rect):target(destination),dc(destination),area(rect){
        const auto memory=CreateCompatibleDC(destination);
        if(!memory)return;
        bitmap=CreateCompatibleBitmap(destination,rect.right,rect.bottom);
        if(!bitmap){DeleteDC(memory);return;}
        dc=memory;previous=SelectObject(dc,bitmap);
    }
    ~PaintBuffer(){
        if(!bitmap)return;
        BitBlt(target,area.left,area.top,area.right-area.left,area.bottom-area.top,dc,area.left,area.top,SRCCOPY);
        SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
    }
};
LRESULT CALLBACK ControlPaintProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR){
    if(message==WM_ERASEBKGND)return 1;
    return DefSubclassProc(window,message,wParam,lParam);
}

RECT Box(int x,int y,int width,int height) {return {app.S(x),app.S(y),app.S(x+width),app.S(y+height)};}
std::wstring StatusText(std::wstring_view text) {
    std::wstring result;
    bool space=false;
    for(const auto ch:text) {
        if(ch==L'.')continue;
        if(ch==L' '||ch==L'\t'||ch==L'\r'||ch==L'\n'){space=!result.empty();continue;}
        if(space){result+=L' ';space=false;}
        result+=ch;
    }
    return result;
}
void Status(std::wstring text,bool error=false) {
    app.status=StatusText(text);app.failed=error;
    const auto area=Box(26,554,660,18);InvalidateRect(app.window,&area,FALSE);
}
std::wstring Wide(std::string_view text) {
    const auto count=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring result(count,L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),count);
    return result;
}
std::string Upper(std::string text) {
    for(auto& c:text)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}
int BindingCode(std::string text) {
    text=Upper(std::move(text));
    if(text=="MOUSE_FORWARD"||text=="MOUSE5"||text=="XBUTTON2")return VK_XBUTTON2;
    if(text=="MOUSE_BACK"||text=="MOUSE4"||text=="XBUTTON1")return VK_XBUTTON1;
    if(text=="MOUSE_LEFT")return VK_LBUTTON;
    if(text=="MOUSE_RIGHT")return VK_RBUTTON;
    if(text=="MOUSE_MIDDLE")return VK_MBUTTON;
    if(text.size()==1 && ((text[0]>='A'&&text[0]<='Z')||(text[0]>='0'&&text[0]<='9')))return text[0];
    if(text.size()>1 && text[0]=='F' && text[1]>='0'&&text[1]<='9')return VK_F1+std::atoi(text.c_str()+1)-1;
    struct Named{const char* name;int vk;};
    const Named names[]={{"BACKSPACE",VK_BACK},{"SPACE",VK_SPACE},{"ENTER",VK_RETURN},{"TAB",VK_TAB},
        {"INSERT",VK_INSERT},{"DELETE",VK_DELETE},{"HOME",VK_HOME},{"END",VK_END},{"PAGEUP",VK_PRIOR},
        {"PAGE_UP",VK_PRIOR},{"PAGEDOWN",VK_NEXT},{"PAGE_DOWN",VK_NEXT},{"UP",VK_UP},{"DOWN",VK_DOWN},
        {"LEFT",VK_LEFT},{"RIGHT",VK_RIGHT},{"ESC",VK_ESCAPE},{"ESCAPE",VK_ESCAPE},{"CAPSLOCK",VK_CAPITAL},
        {"CAPS_LOCK",VK_CAPITAL},{"PAUSE",VK_PAUSE},{"SCROLLLOCK",VK_SCROLL},{"SCROLL_LOCK",VK_SCROLL},
        {"[",VK_OEM_4},{"]",VK_OEM_6},{"\\",VK_OEM_5},{";",VK_OEM_1},{"'",VK_OEM_7},
        {",",VK_OEM_COMMA},{".",VK_OEM_PERIOD},{"/",VK_OEM_2}};
    for(const auto& item:names)if(text==item.name)return item.vk;
    if(text.starts_with("NUMPAD")&&text.size()==7)return VK_NUMPAD0+text[6]-'0';
    char* end{};const auto number=std::strtoul(text.c_str(),&end,0);
    return end!=text.c_str()&&!*end&&number<256?static_cast<int>(number):0;
}
std::string KeyName(int vk,bool mouse=false) {
    if(mouse) {
        switch(vk){case VK_LBUTTON:return "0x01";case VK_RBUTTON:return "0x02";case VK_MBUTTON:return "0x04";
            case VK_XBUTTON1:return "MOUSE_BACK";case VK_XBUTTON2:return "MOUSE_FORWARD";}
    }
    if((vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9'))return std::string(1,static_cast<char>(vk));
    if(vk>=VK_F1&&vk<=VK_F24)return "F"+std::to_string(vk-VK_F1+1);
    if(vk>=VK_NUMPAD0&&vk<=VK_NUMPAD9)return "NUMPAD"+std::to_string(vk-VK_NUMPAD0);
    switch(vk){case VK_BACK:return "BACKSPACE";case VK_RETURN:return "ENTER";case VK_TAB:return "TAB";
        case VK_SPACE:return "SPACE";case VK_INSERT:return "INSERT";case VK_DELETE:return "DELETE";
        case VK_HOME:return "HOME";case VK_END:return "END";case VK_PRIOR:return "PAGEUP";
        case VK_NEXT:return "PAGEDOWN";case VK_UP:return "UP";case VK_DOWN:return "DOWN";
        case VK_LEFT:return "LEFT";case VK_RIGHT:return "RIGHT";case VK_CAPITAL:return "CAPSLOCK";
        case VK_PAUSE:return "PAUSE";case VK_SCROLL:return "SCROLLLOCK";}
    char value[16]{};sprintf_s(value,"0x%02X",vk);return value;
}
std::wstring DisplayBinding(const std::string& key) {
    const auto vk=BindingCode(key);
    switch(vk){case VK_XBUTTON1:return L"MOUSE 4";case VK_XBUTTON2:return L"MOUSE 5";
        case VK_LBUTTON:return L"MOUSE 1";case VK_RBUTTON:return L"MOUSE 2";case VK_MBUTTON:return L"MOUSE 3";
        case VK_SHIFT:return L"SHIFT";case VK_CONTROL:return L"CTRL";case VK_MENU:return L"ALT";}
    switch(vk){case VK_OEM_4:return L"[";case VK_OEM_6:return L"]";case VK_OEM_5:return L"\\";
        case VK_OEM_1:return L";";case VK_OEM_7:return L"'";case VK_OEM_COMMA:return L",";
        case VK_OEM_PERIOD:return L".";case VK_OEM_2:return L"/";}
    return Wide(Upper(key));
}
void Refresh() {
    for(const auto control:{app.enable,app.slider,app.bindings[0],app.bindings[1],app.bindings[2]})
        if(control)InvalidateRect(control,nullptr,FALSE);
}
bool Save();
bool AlreadyRunning();
bool GameExists() {
    const auto attributes=GetFileAttributesW(app.game.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
void AcceptBinding(int vk,bool mouse=false) {
    if(!app.capturing)return;
    if(vk==VK_ESCAPE&&!mouse){app.capturing=0;Refresh();Status(L"");return;}
    auto key=KeyName(vk,mouse);
    // Camera toggle uses the keyboard parser; mouse VKs use its numeric format.
    if(app.capturing==kSwitch&&mouse) {char value[16]{};sprintf_s(value,"0x%02X",vk);key=value;}
    if((app.capturing==kRight&&vk==BindingCode(app.settings.tiltLeft))||
       (app.capturing==kLeft&&vk==BindingCode(app.settings.tiltRight))) {
        Status(L"Tilt left and right must use different keys",true);return;
    }
    if(app.capturing==kSwitch)app.settings.switchCamera=key;
    else if(app.capturing==kRight)app.settings.tiltRight=key;
    else app.settings.tiltLeft=key;
    app.capturing=0;Refresh();Status(L"");
}
float Fade(float progress){return progress*progress*(3.0F-2.0F*progress);}
Gdiplus::Color ElementColor(int id){
    const auto p=Fade(elementHover[id].progress);
    return Gdiplus::Color(255,static_cast<BYTE>(162+51*p+0.5F),
        static_cast<BYTE>(182+43*p+0.5F),static_cast<BYTE>(170+45*p+0.5F));
}
void DrawLiveText(HDC dc,const std::wstring& text,const RECT& rect,Gdiplus::Color color,
    float size=14.0F,bool centered=false,bool underline=false);
void DrawArtwork(HDC dc) {
    RECT client=Box(0,0,kWidth,kHeight);FillRect(dc,&client,app.background);
    {
        Gdiplus::Graphics graphics(dc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        // The PNG already includes the red FIRST PERSON NARRATIVE subtitle.
        graphics.DrawImage(app.artwork.get(),Gdiplus::RectF(223.0F*app.dpi/96.0F,58.0F*app.dpi/96.0F,
            266.0F*app.dpi/96.0F,266.0F*app.artwork->GetHeight()/app.artwork->GetWidth()*app.dpi/96.0F));
    }
    const Gdiplus::Color dim(255,117,122,119);
    DrawLiveText(dc,L"v"+Wide(launcher::kVersion),Box(26,575,60,15),dim,12.0F);
    DrawLiveText(dc,L"v1.2.11",Box(628,575,58,15),dim,12.0F);
}
void AnimateButtons() {
    POINT cursor{};GetCursorPos(&cursor);const auto hovered=WindowFromPoint(cursor);
    const auto now=GetTickCount64();const auto step=static_cast<float>(now-animationTick)/180.0F;animationTick=now;
    if(now-app.gameRunningCheckTick>=500){
        const bool previousRunning=app.gameRunning,previousFound=app.gameFound;
        app.gameFound=GameExists();app.gameRunning=app.gameFound&&AlreadyRunning();app.gameRunningCheckTick=now;
        if(previousRunning!=app.gameRunning||previousFound!=app.gameFound){
            InvalidateRect(app.launch,nullptr,FALSE);InvalidateRect(app.apply,nullptr,FALSE);InvalidateRect(app.reset,nullptr,FALSE);
        }
    }
    if(app.sliderDisplayed!=static_cast<float>(app.sliderPreview)){
        const auto t=std::clamp(static_cast<float>(now-app.sliderAnimationTick)/80.0F,0.0F,1.0F);
        app.sliderDisplayed=t>=1.0F?static_cast<float>(app.sliderPreview):
            app.sliderAnimationFrom+(app.sliderPreview-app.sliderAnimationFrom)*t*t*(3.0F-2.0F*t);
        InvalidateRect(app.slider,nullptr,FALSE);
    }
    HWND controls[]={app.close,app.enable,app.slider,app.apply,app.reset,app.launch,app.patreon,
        app.bindings[0],app.bindings[1],app.bindings[2]};
    for(const auto control:controls){
        const auto id=GetDlgCtrlID(control);
        const bool disabled=(!app.gameFound&&(control==app.launch||control==app.apply||control==app.reset))||
            (control==app.launch&&app.gameRunning);
        const bool active=(hovered==control||(control==app.slider&&app.sliderDragging))&&!disabled;
        auto& progress=elementHover[id].progress;const auto previous=progress;
        progress=std::clamp(progress+(active?step:-step),0.0F,1.0F);
        if(progress!=previous)InvalidateRect(control,nullptr,FALSE);
    }
    for(int i=0;i<3;++i){
        auto& progress=captureFade[i].progress;const auto previous=progress;
        progress=std::clamp(progress+(app.capturing==kSwitch+i?step:-step),0.0F,1.0F);
        if(progress!=previous)InvalidateRect(app.bindings[i],nullptr,FALSE);
    }
}
void DrawLiveText(HDC dc,const std::wstring& text,const RECT& rect,Gdiplus::Color color,float size,bool centered,bool underline) {
    Gdiplus::Graphics graphics(dc);graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::FontFamily family;int count{};app.uiFonts->GetFamilies(1,&family,&count);
    if(!count)return;
    const auto style=underline?Gdiplus::FontStyleUnderline:Gdiplus::FontStyleRegular;
    Gdiplus::Font font(&family,size*app.dpi/96.0F,style,Gdiplus::UnitPixel);
    const Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat format;
    format.SetAlignment(centered?Gdiplus::StringAlignmentCenter:Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    graphics.DrawString(text.c_str(),-1,&font,Gdiplus::RectF(static_cast<float>(rect.left),static_cast<float>(rect.top),
        static_cast<float>(rect.right-rect.left),static_cast<float>(rect.bottom-rect.top)),&format,&brush);
}
void Layout() {
    if(app.font)DeleteObject(app.font);
    app.font=CreateFontW(-app.S(14),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    Gdiplus::FontFamily family;
    int count{};
    app.statusFonts->GetFamilies(1,&family,&count);
    if(count)app.statusFont=std::make_unique<Gdiplus::Font>(&family,12.0F*app.dpi/96.0F,
        Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
    const auto move=[](HWND control,int x,int y,int w,int h){
        MoveWindow(control,app.S(x),app.S(y),app.S(w),app.S(h),TRUE);
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(app.font),TRUE);
    };
    move(app.close,668,8,36,36);move(app.enable,160,184,432,38);
    move(app.slider,160,219,432,49);
    move(app.bindings[0],160,265,432,48);move(app.bindings[1],160,310,432,48);move(app.bindings[2],160,355,432,48);
    move(app.apply,223,413,266,28);move(app.reset,223,441,266,28);move(app.launch,223,514,266,42);move(app.patreon,284,575,144,15);
    InvalidateRect(app.window,nullptr,TRUE);
}
bool AlreadyRunning() {
    const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool running=false;
    if(Process32FirstW(snapshot,&entry))do{
        if(_wcsicmp(entry.szExeFile,L"AlanWake2.exe")==0){running=true;break;}
    }while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);return running;
}
bool Save() {
    if(!app.configReady){Status(L"Settings could not be initialized so reopen the launcher after fixing the config file",true);return false;}
    if(!app.resetAll&&app.settings==app.initialSettings)return true;
    const auto right=BindingCode(app.settings.tiltRight),left=BindingCode(app.settings.tiltLeft);
    if(right&&right==left){Status(L"Tilt left and right must use different keys",true);return false;}
    std::wstring error;
    if(!launcher::SaveSettings(app.ini,app.settings,app.defaults,app.resetAll,error)){Status(error,true);return false;}
    app.resetAll=false;app.initialSettings=app.settings;Status(L"");return true;
}
void Launch() {
    if(AlreadyRunning()){Status(L"Game is already running so restart it to apply Enable",true);return;}
    const auto attrs=GetFileAttributesW(app.game.c_str());
    if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_DIRECTORY)){Status(L"Game executable was not found next to the launcher",true);return;}
    if(!Save())return;
    std::wstring command=L"\""+app.game.wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(app.game.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,app.directory.c_str(),&startup,&process)){
        Status(L"Could not start the game (error "+std::to_wstring(GetLastError())+L")",true);return;
    }
    CloseHandle(process.hThread);CloseHandle(process.hProcess);DestroyWindow(app.window);
}
void SliderValue(int value) {
    value=std::clamp(value,0,100);
    if(value==app.sliderPreview)return;
    app.sliderAnimationFrom=app.sliderDisplayed;app.sliderAnimationTick=GetTickCount64();
    app.sliderPreview=value;InvalidateRect(app.slider,nullptr,FALSE);
}
void CommitSlider() {
    if(app.settings.sensitive==app.sliderPreview)return;
    app.settings.sensitive=app.sliderPreview;
}
LRESULT CALLBACK SliderProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR) {
    switch(message){
    case TBM_GETPOS:return app.sliderPreview;
    case TBM_SETPOS:SliderValue(static_cast<int>(lParam));app.settings.sensitive=app.sliderPreview;return 0;
    case WM_GETDLGCODE:return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN:
        if(GET_X_LPARAM(lParam)<app.S(177)){SetFocus(window);return 0;}
        SetFocus(window);app.sliderDragging=true;SetCapture(window);
        SliderValue(MulDiv(GET_X_LPARAM(lParam)-app.S(177),100,std::max(1,app.S(206))));return 0;
    case WM_MOUSEMOVE:
        if(app.sliderDragging&&GetCapture()==window)SliderValue(MulDiv(GET_X_LPARAM(lParam)-app.S(177),100,std::max(1,app.S(206))));
        return 0;
    case WM_LBUTTONUP:
        if(app.sliderDragging){
            SliderValue(MulDiv(GET_X_LPARAM(lParam)-app.S(177),100,std::max(1,app.S(206))));
            app.sliderDragging=false;if(GetCapture()==window)ReleaseCapture();CommitSlider();
        }
        return 0;
    case WM_CAPTURECHANGED:case WM_CANCELMODE:
        if(app.sliderDragging){app.sliderDragging=false;SliderValue(app.settings.sensitive);}
        if(message==WM_CANCELMODE&&GetCapture()==window)ReleaseCapture();
        return 0;
    case WM_KEYDOWN:
        switch(wParam){case VK_LEFT:case VK_DOWN:app.sliderKeyboardEditing=true;SliderValue(app.sliderPreview-1);return 0;
            case VK_RIGHT:case VK_UP:app.sliderKeyboardEditing=true;SliderValue(app.sliderPreview+1);return 0;
            case VK_PRIOR:app.sliderKeyboardEditing=true;SliderValue(app.sliderPreview+10);return 0;
            case VK_NEXT:app.sliderKeyboardEditing=true;SliderValue(app.sliderPreview-10);return 0;
            case VK_HOME:app.sliderKeyboardEditing=true;SliderValue(0);return 0;
            case VK_END:app.sliderKeyboardEditing=true;SliderValue(100);return 0;}
        break;
    case WM_KEYUP:
        if(app.sliderKeyboardEditing){app.sliderKeyboardEditing=false;CommitSlider();return 0;}
        break;
    case WM_KILLFOCUS:
        if(app.sliderKeyboardEditing){app.sliderKeyboardEditing=false;CommitSlider();}
        break;
    }
    return DefSubclassProc(window,message,wParam,lParam);
}
void DrawButtonContent(const DRAWITEMSTRUCT& item) {
    FillRect(item.hDC,&item.rcItem,app.background);
    const auto id=static_cast<int>(item.CtlID);
    const auto color=ElementColor(id);const auto scale=app.dpi/96.0F;
    if(id==kEnable){
        DrawLiveText(item.hDC,L"FIRST PERSON",Box(0,0,170,38),color);
        Gdiplus::Graphics graphics(item.hDC);
        const Gdiplus::Pen pen(color,2.0F*scale);
        graphics.DrawRectangle(&pen,178.0F*scale,11.0F*scale,16.0F*scale,16.0F*scale);
        if(app.settings.enabled){
            const Gdiplus::SolidBrush marker(color);
            graphics.FillRectangle(&marker,182.0F*scale,15.0F*scale,8.0F*scale,8.0F*scale);
        }
    }else if(id==kSlider){
        DrawLiveText(item.hDC,L"CAMERA MOTION",Box(0,0,165,49),color);
        {
            Gdiplus::Graphics graphics(item.hDC);
            const Gdiplus::SolidBrush dim(Gdiplus::Color(128,162,182,170)),full(color);
            graphics.FillRectangle(&dim,177.0F*scale,15.5F*scale,206.0F*scale,18.0F*scale);
            graphics.FillRectangle(&full,177.0F*scale,15.5F*scale,206.0F*scale*app.sliderDisplayed/100.0F,18.0F*scale);
        }
        DrawLiveText(item.hDC,std::to_wstring(static_cast<int>(app.sliderDisplayed+0.5F))+L"%",Box(389,0,43,49),color);
    }else if(id>=kSwitch&&id<=kLeft){
        const wchar_t* labels[]={L"SWITCH CAMERA",L"LEAN RIGHT",L"LEAN LEFT"};
        DrawLiveText(item.hDC,labels[id-kSwitch],Box(0,0,170,48),color);
        {
            Gdiplus::Graphics graphics(item.hDC);
            const auto capture=Fade(captureFade[id-kSwitch].progress);
            const Gdiplus::SolidBrush fill(Gdiplus::Color(static_cast<BYTE>(10+3*capture+0.5F),162,182,170));
            graphics.FillRectangle(&fill,177.0F*scale,10.0F*scale,207.0F*scale,28.0F*scale);
            const Gdiplus::Pen outline(Gdiplus::Color(static_cast<BYTE>(128*Fade(elementHover[id].progress)+0.5F),162,182,170),scale);
            graphics.DrawRectangle(&outline,177.5F*scale,10.5F*scale,206.0F*scale,27.0F*scale);
        }
        const auto& value=id==kSwitch?app.settings.switchCamera:id==kRight?app.settings.tiltRight:app.settings.tiltLeft;
        DrawLiveText(item.hDC,app.capturing==id?L"...":DisplayBinding(value),Box(181,10,199,28),color,14.0F,true);
    }else if(id==kLaunch){
        {
            Gdiplus::Graphics graphics(item.hDC);const Gdiplus::SolidBrush fill((!app.gameFound||app.gameRunning)?
                Gdiplus::Color(10,162,182,170):color);
            graphics.FillRectangle(&fill,0.0F,0.0F,266.0F*scale,42.0F*scale);
        }
        DrawLiveText(item.hDC,!app.gameFound?L"GAME NOT FOUND":app.gameRunning?L"ALREADY RUNNING":L"PLAY",item.rcItem,
            (!app.gameFound||app.gameRunning)?Gdiplus::Color(128,162,182,170):Gdiplus::Color(255,25,26,27),14.0F,true);
    }else if(id==kApply){
        DrawLiveText(item.hDC,L"APPLY",item.rcItem,app.gameFound?color:Gdiplus::Color(128,162,182,170),14.0F,true);
    }else if(id==kReset){
        DrawLiveText(item.hDC,L"RESTORE DEFAULTS",item.rcItem,app.gameFound?color:Gdiplus::Color(128,162,182,170),14.0F,true);
    }else if(id==kPatreon){
        DrawLiveText(item.hDC,L"patreon.com/c/f1nked",item.rcItem,color,12.0F,false,true);
    }else if(id==kClose){
        Gdiplus::Graphics graphics(item.hDC);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const Gdiplus::Pen pen(color,2.0F*scale);
        graphics.DrawLine(&pen,11.0F*scale,11.0F*scale,25.0F*scale,25.0F*scale);
        graphics.DrawLine(&pen,25.0F*scale,11.0F*scale,11.0F*scale,25.0F*scale);
    }
}
void DrawButton(const DRAWITEMSTRUCT& item) {
    PaintBuffer buffer(item.hDC,item.rcItem);
    auto frame=item;frame.hDC=buffer.dc;DrawButtonContent(frame);
}
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message){
    case WM_CREATE:{
        app.window=window;
        const auto button=[&](const wchar_t* text,int id){
            const auto control=CreateWindowExW(0,L"BUTTON",text,
                WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
            SetWindowSubclass(control,ControlPaintProc,3,0);return control;
        };
        app.close=button(L"Close",kClose);app.enable=button(L"Enable",kEnable);app.slider=button(L"Camera shake",kSlider);
        SetWindowSubclass(app.slider,SliderProc,1,0);
        app.bindings[0]=button(L"Switch camera",kSwitch);app.bindings[1]=button(L"Tilt right",kRight);app.bindings[2]=button(L"Tilt left",kLeft);
        app.apply=button(L"Apply",kApply);app.reset=button(L"Default setting",kReset);app.launch=button(L"Start",kLaunch);app.patreon=button(L"patreon.com/c/f1nked",kPatreon);
        app.gameFound=GameExists();app.gameRunning=app.gameFound&&AlreadyRunning();app.gameRunningCheckTick=GetTickCount64();
        animationTick=GetTickCount64();SetTimer(window,0x484F,15,nullptr);
        Layout();return 0;
    }
    case WM_TIMER:if(wParam==0x484F){AnimateButtons();return 0;}break;
    case WM_COMMAND:
        switch(LOWORD(wParam)){
        case kClose:case IDCANCEL:DestroyWindow(window);return 0;
        case kEnable:app.capturing=0;app.settings.enabled=!app.settings.enabled;Refresh();Status(L"");return 0;
        case kReset:if(!app.gameFound)return 0;{
            const auto previous=app.settings;const bool previousReset=app.resetAll;
            app.capturing=0;app.settings=launcher::ReadSettings(app.defaults);app.resetAll=true;
            if(!Save()){
                app.settings=previous;app.resetAll=previousReset;Refresh();return 0;
            }
            SliderValue(app.settings.sensitive);Refresh();return 0;
        }
        case kApply:if(app.gameFound){app.capturing=0;Refresh();Save();}return 0;
        case kLaunch:if(app.gameFound&&!app.gameRunning){app.capturing=0;Refresh();Launch();}return 0;
        case kSwitch:case kRight:case kLeft:
            app.capturing=LOWORD(wParam);Refresh();Status(L"Press a key or mouse button or press Esc to cancel");return 0;
        case kPatreon:
            ShellExecuteW(window,L"open",L"https://www.patreon.com/c/f1nked",nullptr,nullptr,SW_SHOWNORMAL);return 0;
        }
        break;
    case WM_HSCROLL:if(reinterpret_cast<HWND>(lParam)==app.slider){Refresh();return 0;}break;
    case WM_DRAWITEM:DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));return TRUE;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{
        PAINTSTRUCT paint{};const auto target=BeginPaint(window,&paint);
        {
        RECT client{};GetClientRect(window,&client);PaintBuffer buffer(target,client);const auto dc=buffer.dc;
        DrawArtwork(dc);
        if(!app.status.empty()&&app.statusFont){
            Gdiplus::Graphics graphics(dc);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            const Gdiplus::SolidBrush brush(Gdiplus::Color(128,162,182,170));
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentNear);format.SetLineAlignment(Gdiplus::StringAlignmentNear);
            format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
            format.SetTrimming(Gdiplus::StringTrimmingNone);
            const auto rect=Box(26,554,660,18);
            graphics.DrawString(app.status.c_str(),-1,app.statusFont.get(),Gdiplus::RectF(static_cast<float>(rect.left),
                static_cast<float>(rect.top),static_cast<float>(rect.right-rect.left),static_cast<float>(rect.bottom-rect.top)),&format,&brush);
        }
        }
        EndPaint(window,&paint);return 0;
    }
    case WM_NCHITTEST:{
        POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ScreenToClient(window,&point);
        if(point.y<app.S(146)&&point.x<app.S(668))return HTCAPTION;
        break;
    }
    case WM_DPICHANGED:{
        app.dpi=HIWORD(wParam);const auto rect=reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window,nullptr,rect->left,rect->top,app.S(kWidth),app.S(kHeight),SWP_NOZORDER|SWP_NOACTIVATE);Layout();return 0;
    }
    case WM_DESTROY:KillTimer(window,0x484F);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
bool LoadArtwork(HINSTANCE instance,int id,std::unique_ptr<Gdiplus::Bitmap>& bitmap,IStream*& stream) {
    const auto resource=FindResourceW(instance,MAKEINTRESOURCEW(id),RT_RCDATA);
    const auto loaded=resource?LoadResource(instance,resource):nullptr;
    const auto bytes=loaded?LockResource(loaded):nullptr;
    if(!bytes)return false;
    const auto size=SizeofResource(instance,resource);const auto memory=GlobalAlloc(GMEM_MOVEABLE,size);
    if(!memory)return false;
    const auto pointer=GlobalLock(memory);if(!pointer){GlobalFree(memory);return false;}
    CopyMemory(pointer,bytes,size);GlobalUnlock(memory);
    if(FAILED(CreateStreamOnHGlobal(memory,TRUE,&stream))){GlobalFree(memory);return false;}
    bitmap.reset(Gdiplus::Bitmap::FromStream(stream,FALSE));
    return bitmap&&bitmap->GetLastStatus()==Gdiplus::Ok;
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);app.dpi=GetDpiForSystem();
    Gdiplus::GdiplusStartupInput graphicsInput;ULONG_PTR graphicsToken{};
    if(Gdiplus::GdiplusStartup(&graphicsToken,&graphicsInput,nullptr)!=Gdiplus::Ok)return 1;
    if(!LoadArtwork(instance,102,app.artwork,app.artworkStream))return 2;
    const auto fontResource=FindResourceW(instance,MAKEINTRESOURCEW(103),RT_RCDATA);
    const auto loadedFont=fontResource?LoadResource(instance,fontResource):nullptr;
    const auto fontBytes=loadedFont?LockResource(loadedFont):nullptr;
    if(!fontBytes)return 2;
    app.statusFonts=std::make_unique<Gdiplus::PrivateFontCollection>();
    if(app.statusFonts->AddMemoryFont(fontBytes,static_cast<INT>(SizeofResource(instance,fontResource)))!=Gdiplus::Ok)return 2;
    const auto uiResource=FindResourceW(instance,MAKEINTRESOURCEW(105),RT_RCDATA);
    const auto uiLoaded=uiResource?LoadResource(instance,uiResource):nullptr;
    const auto uiBytes=uiLoaded?LockResource(uiLoaded):nullptr;
    if(!uiBytes)return 2;
    app.uiFonts=std::make_unique<Gdiplus::PrivateFontCollection>();
    if(app.uiFonts->AddMemoryFont(uiBytes,static_cast<INT>(SizeofResource(instance,uiResource)))!=Gdiplus::Ok)return 2;
    wchar_t filename[32768]{};const auto length=GetModuleFileNameW(nullptr,filename,static_cast<DWORD>(std::size(filename)));
    if(!length||length>=std::size(filename))return 3;
    app.directory=std::filesystem::path(filename).parent_path();app.ini=FirstPersonNarrativeConfigPath();app.game=app.directory/L"AlanWake2.exe";app.gameFound=GameExists();
    const auto resource=FindResourceW(instance,MAKEINTRESOURCEW(101),RT_RCDATA);const auto loaded=resource?LoadResource(instance,resource):nullptr;
    const auto data=loaded?LockResource(loaded):nullptr;if(!data)return 4;
    app.defaults.assign(static_cast<const char*>(data),SizeofResource(instance,resource));
    std::string current;std::wstring error;
    app.settings=launcher::ReadSettings(app.defaults);
    if(!app.gameFound) {
        // Do not create a user configuration for a launcher copied outside the game folder.
    } else if(app.ini.empty())error=L"Cannot determine the LocalAppData settings directory";
    else {
        std::error_code directoryError;
        std::filesystem::create_directories(app.ini.parent_path(),directoryError);
        if(directoryError)error=L"Cannot create the settings directory";
        else {
            if(launcher::LoadCurrentSettings(app.ini,app.defaults,current,error)){
                app.settings=launcher::ReadSettings(current);
                app.configReady=true;
            }
        }
    }
    if(!app.configReady&&app.gameFound){app.status=StatusText(error);app.failed=true;}
    app.initialSettings=app.settings;
    app.sliderPreview=app.settings.sensitive;
    app.sliderDisplayed=app.sliderAnimationFrom=static_cast<float>(app.sliderPreview);
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    app.background=CreateSolidBrush(kBackground);
    WNDCLASSEXW type{};type.cbSize=sizeof(type);type.lpfnWndProc=WindowProc;type.hInstance=instance;
    type.hIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED));
    type.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
    type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hbrBackground=app.background;type.lpszClassName=L"AW2FirstPersonLauncher";
    if(!RegisterClassExW(&type))return 5;
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);
    const auto width=app.S(kWidth),height=app.S(kHeight);
    const int x=monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2;
    const int y=monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2;
    HWND window=CreateWindowExW(WS_EX_APPWINDOW,type.lpszClassName,L"FirstPersonNarrativeLauncher",
        WS_POPUP|WS_CLIPCHILDREN|WS_MINIMIZEBOX,x,y,width,height,nullptr,nullptr,instance,nullptr);
    if(!window)return 6;ShowWindow(window,show);UpdateWindow(window);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0){
        if(app.capturing&&message.hwnd!=app.close){
            if(message.message==WM_KEYDOWN||message.message==WM_SYSKEYDOWN){AcceptBinding(static_cast<int>(message.wParam));continue;}
            int mouse=0;
            if(message.message==WM_LBUTTONDOWN)mouse=VK_LBUTTON;
            else if(message.message==WM_RBUTTONDOWN)mouse=VK_RBUTTON;
            else if(message.message==WM_MBUTTONDOWN)mouse=VK_MBUTTON;
            else if(message.message==WM_XBUTTONDOWN)mouse=HIWORD(message.wParam)==XBUTTON1?VK_XBUTTON1:VK_XBUTTON2;
            if(mouse){AcceptBinding(mouse,true);continue;}
        }
        if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    DeleteObject(app.font);DeleteObject(app.background);app.artwork.reset();
    app.uiFonts.reset();
    app.statusFont.reset();app.statusFonts.reset();
    if(app.artworkStream)app.artworkStream->Release();Gdiplus::GdiplusShutdown(graphicsToken);
    return static_cast<int>(message.wParam);
}
