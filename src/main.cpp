#include "client.hpp"
#include "ui.hpp"
#include "avatar.hpp"
#include "preview.hpp"
#include "update.hpp"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <Windows.h>
#include <windowsx.h>
#include <cstring>
#include <d3d11.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <chrono>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
using Microsoft::WRL::ComPtr;
namespace noctra {
namespace {
void check(HRESULT code,const char* label){if(FAILED(code))throw std::runtime_error(std::string(label)+" failed: "+std::to_string(code));}
struct Surface {
    HWND window{};ImGuiContext* context{};bool overlay{},backend{};
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> device_context;
    ComPtr<IDXGISwapChain> swap;ComPtr<ID3D11RenderTargetView> target;
    UINT width{},height{};
    UINT dpi{96};
    void current(){ImGui::SetCurrentContext(context);}
    void render_target(){ComPtr<ID3D11Texture2D> buffer;check(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)),"GetBuffer");
        check(device->CreateRenderTargetView(buffer.Get(),nullptr,&target),"CreateRenderTargetView");}
    void resize(UINT w,UINT h){if(!w||!h||(w==width&&h==height))return;
        device_context->OMSetRenderTargets(0,nullptr,nullptr);target.Reset();check(swap->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0),"ResizeBuffers");
        width=w;height=h;render_target();}
    void frame(){current();ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();}
    void present(UINT sync){current();ImGui::Render();const float clear[4]={overlay?0.0f:0.03f,overlay?0.0f:0.035f,overlay?0.0f:0.05f,overlay?0.0f:1.0f};
        auto* rt=target.Get();device_context->OMSetRenderTargets(1,&rt,nullptr);device_context->ClearRenderTargetView(rt,clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());const auto hr=swap->Present(sync,0);
        if(hr!=DXGI_STATUS_OCCLUDED)check(hr,"Present");}
    ~Surface(){if(context){current();if(backend){ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();}ImGui::DestroyContext(context);context=nullptr;}
        if(window){SetWindowLongPtrW(window,GWLP_USERDATA,0);DestroyWindow(window);window=nullptr;}}
};
LRESULT CALLBACK procedure(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    auto* surface=reinterpret_cast<Surface*>(GetWindowLongPtrW(w,GWLP_USERDATA));
    if(surface&&surface->context){surface->current();if(!surface->overlay&&ImGui_ImplWin32_WndProcHandler(w,msg,wp,lp))return 1;}
    if(msg==WM_NCHITTEST&&surface&&surface->overlay)return HTTRANSPARENT;
    if(msg==WM_MOUSEACTIVATE&&surface&&surface->overlay)return MA_NOACTIVATE;
    if(msg==WM_NCCALCSIZE&&wp&&surface&&!surface->overlay){
        if(IsZoomed(w)){auto* params=reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
            if(GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&monitor))params->rgrc[0]=monitor.rcWork;}
        return 0;
    }
    if(msg==WM_GETMINMAXINFO&&surface&&!surface->overlay){auto* info=reinterpret_cast<MINMAXINFO*>(lp);
        const UINT dpi=GetDpiForWindow(w);info->ptMinTrackSize={MulDiv(805,static_cast<int>(dpi),96),MulDiv(480,static_cast<int>(dpi),96)};
        MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
        if(GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&monitor)){
            info->ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};
            info->ptMaxSize={monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};}
        return 0;
    }
    if(msg==WM_NCHITTEST&&surface&&!surface->overlay){RECT r{};GetWindowRect(w,&r);
        const int x=GET_X_LPARAM(lp)-r.left,y=GET_Y_LPARAM(lp)-r.top;
        const int scale=static_cast<int>(GetDpiForWindow(w));const int edge=MulDiv(7,scale,96);
        if(!IsZoomed(w)){const bool left=x<edge,right=x>=r.right-r.left-edge,top=y<edge,bottom=y>=r.bottom-r.top-edge;
            if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;
            if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;
            if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;}
        // Reserve the right side for real ImGui minimize/close controls.
        if(y<MulDiv(40,scale,96)&&x>=MulDiv(50,scale,96)&&x<r.right-r.left-MulDiv(85,scale,96))return HTCAPTION;
        return HTCLIENT;
    }
    if(msg==WM_CLOSE){PostQuitMessage(0);return 0;}
    if(msg==WM_SYSCOMMAND&&(wp&0xfff0)==SC_KEYMENU)return 0;
    if(msg==WM_DPICHANGED&&surface&&!surface->overlay){const auto* r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
    return DefWindowProcW(w,msg,wp,lp);
}
void create(Surface& s,HINSTANCE instance,bool overlay) {
    s.overlay=overlay;
    const DWORD style=overlay?WS_POPUP:(WS_OVERLAPPEDWINDOW&~WS_CAPTION);
    const DWORD extended=overlay?(WS_EX_TOPMOST|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW):(WS_EX_APPWINDOW|WS_EX_TOPMOST);
    RECT r{0,0,805,520};
    s.window=CreateWindowExW(extended,L"NoctraNativeWindow",overlay?L"NOCTRA Overlay":L"NOCTRA - External",style,
        overlay?0:120,overlay?0:80,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
    if(!s.window)throw std::runtime_error("Window creation failed");
    SetWindowLongPtrW(s.window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
    if(!overlay){const auto native_style=GetWindowLongPtrW(s.window,GWL_STYLE);
        SetWindowLongPtrW(s.window,GWL_STYLE,native_style&~static_cast<LONG_PTR>(WS_CAPTION));
        SetWindowPos(s.window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        // Rounded corners are optional; unsupported OS versions retain a normal border.
        const DWORD corner=DWMWCP_ROUND;DwmSetWindowAttribute(s.window,DWMWA_WINDOW_CORNER_PREFERENCE,&corner,sizeof(corner));}
    if(overlay){if(!SetLayeredWindowAttributes(s.window,0,255,LWA_ALPHA))throw std::runtime_error("Layered window setup failed");
        const MARGINS margins{-1,-1,-1,-1};check(DwmExtendFrameIntoClientArea(s.window,&margins),"DWM transparency");}
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=2;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow=s.window;desc.SampleDesc.Count=1;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL feature{};const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0};
    check(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,2,D3D11_SDK_VERSION,
        &desc,&s.swap,&s.device,&feature,&s.device_context),"D3D11 initialization");
    RECT actual{};GetClientRect(s.window,&actual);s.width=static_cast<UINT>(actual.right);s.height=static_cast<UINT>(actual.bottom);s.render_target();
    s.context=ImGui::CreateContext();s.current();ImGui::GetIO().IniFilename=nullptr;ImGui::GetIO().LogFilename=nullptr;
    ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    s.dpi=GetDpiForWindow(s.window);
    const float scale=static_cast<float>(s.dpi)/96.0f;
    if(!overlay)initialize_menu_fonts(scale);
    else if(std::filesystem::exists(L"C:\\Windows\\Fonts\\segoeui.ttf"))ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf",15.0f*scale);
    else ImGui::GetIO().Fonts->AddFontDefault();
    apply_theme();ImGui::GetStyle().ScaleAllSizes(scale);
    if(!ImGui_ImplWin32_Init(s.window)||!ImGui_ImplDX11_Init(s.device.Get(),s.device_context.Get()))throw std::runtime_error("ImGui backend initialization failed");
    s.backend=true;
}
std::uint64_t cpu_time(){FILETIME creation{},exit{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user))throw std::runtime_error("Process timing unavailable");
    ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return k.QuadPart+u.QuadPart;}
struct Options {bool diagnose{},smoke{},benchmark{};int seconds{10};std::filesystem::path output{"artifacts/native-diagnostic.txt"};};
Options options(){Options o;const std::wstring command=GetCommandLineW();
    o.diagnose=command.find(L"--diagnose")!=std::wstring::npos;o.smoke=command.find(L"--smoke-test")!=std::wstring::npos;
    o.benchmark=command.find(L"--benchmark")!=std::wstring::npos;
    if(o.smoke)o.output="artifacts/native-smoke.txt";
    if(o.benchmark){o.seconds=20;o.output="artifacts/native-benchmark.txt";}return o;}
int execute(HINSTANCE instance) {
    if(const auto helper=run_update_helper())return *helper;
    const auto opt=options();Client client;
    if(opt.diagnose){const auto deadline=Clock::now()+std::chrono::seconds(opt.seconds);Snapshot s;
        do{s=client.snapshot();if(s.ready)break;std::this_thread::sleep_for(std::chrono::milliseconds(100));}while(Clock::now()<deadline);
        write_diagnostic(s,opt.output);std::cout<<"diagnostic_status="<<s.status<<"\nready="<<s.ready<<"\nentities="<<s.entities.size()<<std::endl;return s.ready?0:2;}
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=procedure;wc.hInstance=instance;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"NoctraNativeWindow";
    if(!RegisterClassExW(&wc))throw std::runtime_error("Window class registration failed");
    Surface menu,overlay;create(menu,instance,false);create(overlay,instance,true);
    ShowWindow(menu.window,SW_SHOW);UpdateWindow(menu.window);
    Settings settings;UiState ui;bool menu_open=true,previous_menu_key=false;
    UpdateService updates(!opt.smoke&&!opt.benchmark);bool health_confirmed=false;
    AvatarLoader avatars;std::string avatar_name;std::uint64_t avatar_revision{};
    ComPtr<ID3D11ShaderResourceView> avatar_texture;
    std::string avatar_error;std::size_t avatar_frames{};
    const auto update_avatar=[&](const Snapshot& snapshot,Clock::time_point now){
        const auto* local=preview_local_entity(snapshot,now);
        // Preserve the account during brief respawn gaps, but never draw its
        // cached texture without a fresh local entity. Clear it on disconnect.
        const std::string name=local?local->name:(snapshot.compatible?avatar_name:std::string{});
        if(name!=avatar_name){avatar_name=name;avatar_texture.Reset();ui.avatar_texture=0;ui.preview_geometry=false;avatar_error.clear();}
        avatars.request(name,ui.avatar_refresh);ui.avatar_refresh=false;
        const auto result=avatars.result();ui.avatar_loading=result.loading;
        if(result.loading||result.revision==avatar_revision){ui.avatar_failed=!avatar_error.empty();return;}
        avatar_revision=result.revision;avatar_texture.Reset();ui.avatar_texture=0;
        avatar_error=result.error;
        if(result.image){
            const auto& image=*result.image;D3D11_TEXTURE2D_DESC desc{};
            desc.Width=image.width;desc.Height=image.height;desc.MipLevels=1;desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{};data.pSysMem=image.rgba.data();data.SysMemPitch=image.width*4;
            ComPtr<ID3D11Texture2D> texture;
            const auto created=menu.device->CreateTexture2D(&desc,&data,&texture);
            const auto view=SUCCEEDED(created)?menu.device->CreateShaderResourceView(texture.Get(),nullptr,&avatar_texture):created;
            if(FAILED(view)){ui.avatar_failed=true;avatar_error="Avatar GPU upload failed: "+std::to_string(view);return;}
            ui.avatar_texture=reinterpret_cast<std::uintptr_t>(avatar_texture.Get());ui.avatar_width=image.width;ui.avatar_height=image.height;
        }
        ui.avatar_failed=!avatar_error.empty();
    };
    const auto start=Clock::now();auto previous=start;auto cpu_start=cpu_time();
    bool startup=!opt.smoke&&!opt.benchmark,startup_download_requested=false,startup_install_requested=false;
    std::vector<double> frames,reads;Snapshot last;std::size_t ready_frames{},input_events{};
    std::optional<Target> retained;Vec2 remainder{};bool was_active=false;RECT prior_area{};
    std::uint64_t aim_place{};std::uintptr_t aim_local_character{};
    bool running=true;unsigned smoke_mask=0;
    Clock::time_point last_target_time{};
    while(running&&!ui.quit) {
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)running=false;TranslateMessage(&message);DispatchMessageW(&message);}
        if(!running)break;
        if(startup){
            const auto startup_elapsed=std::chrono::duration<double>(Clock::now()-start).count();
            const auto before=updates.status();
            if(before.phase==UpdatePhase::available&&!startup_download_requested){startup_download_requested=true;updates.download();}
            else if(before.phase==UpdatePhase::staged&&!startup_install_requested){
                startup_install_requested=true;std::string error;
                if(updates.install(error))return 0;
            }
            const auto status=updates.status();
            if(status.phase==UpdatePhase::complete)startup=false;
            else if(status.phase==UpdatePhase::failed&&startup_elapsed>=1.5)startup=false;
            else if(startup_elapsed>=25.0)startup=false;
            if(startup){
                menu.frame();draw_startup(status.message,status.version,status.busy,status.available,status.staged,status.phase==UpdatePhase::failed,startup_elapsed);menu.present(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(16));continue;
            }
        }
        const auto now=Clock::now();const float dt=std::chrono::duration<float>(now-previous).count();previous=now;
        const double elapsed=std::chrono::duration<double>(now-start).count();
        if((opt.smoke||opt.benchmark)&&elapsed>opt.seconds)break;
        last=client.snapshot();const auto game=reinterpret_cast<HWND>(last.window);
        std::uintptr_t local_character{};for(const auto& entity:last.entities)if(entity.local)local_character=entity.character;
        if(last.place!=aim_place||local_character!=aim_local_character){retained.reset();remainder={};}
        aim_place=last.place;aim_local_character=local_character;
        const bool game_focused=game&&GetForegroundWindow()==game;
        const bool key_down=(GetAsyncKeyState(settings.menu_key)&0x8000)!=0;
        if(key_down&&!previous_menu_key&&(game_focused||GetForegroundWindow()==menu.window)){
            menu_open=!menu_open;ShowWindow(menu.window,menu_open?SW_SHOW:SW_HIDE);
            if(menu_open)SetWindowPos(menu.window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
            if(menu_open)SetForegroundWindow(menu.window);
        }previous_menu_key=key_down;
        if(opt.smoke){ui.tab=std::min(4,static_cast<int>(elapsed/2));smoke_mask|=1u<<ui.tab;
            if(ui.tab==1){settings.skeleton=true;settings.tracers=true;}
            if(ui.tab==2)settings.opacity=0.90f;
            if(ui.tab==3){settings.color={0.3f,0.75f,0.9f,1};}
        }
        RECT area{};POINT origin{};bool has_area=game&&IsWindow(game)&&!IsIconic(game)&&GetClientRect(game,&area)&&ClientToScreen(game,&origin);
        const bool fresh=last.ready&&std::chrono::duration<double>(now-last.captured).count()<0.15;
        const bool active=has_area&&game_focused&&fresh;
        if(has_area){area.left=origin.x;area.top=origin.y;area.right+=origin.x;area.bottom+=origin.y;
            if(std::memcmp(&area,&prior_area,sizeof(area))!=0){SetWindowPos(overlay.window,HWND_TOPMOST,area.left,area.top,area.right-area.left,area.bottom-area.top,SWP_NOACTIVATE);prior_area=area;}}
        ShowWindow(overlay.window,active?SW_SHOWNOACTIVATE:SW_HIDE);
        const Vec2 viewport{static_cast<float>(area.right-area.left),static_cast<float>(area.bottom-area.top)};
        const bool aim_active=active&&!last.geometry_only&&!menu_open&&settings.aim&&!opt.smoke&&!opt.benchmark&&(GetAsyncKeyState(settings.aim_key)&0x8000)!=0;
        if(aim_active){
            // Get mouse position relative to game window
            POINT mousePos;
            GetCursorPos(&mousePos);
            ScreenToClient(game, &mousePos);
            Vec2 mouseVec{static_cast<float>(mousePos.x), static_cast<float>(mousePos.y)};
            auto chosen=choose_target(last,settings,viewport,retained?retained->id:0,retained?retained->character:0,&mouseVec);
            if(!chosen||!retained||chosen->id!=retained->id||chosen->character!=retained->character)remainder={};
            // Prediction and history handling
            if (settings.predict_enabled && retained && retained->has_previous) {
                auto delta_time = std::chrono::duration<float>(now - last_target_time).count();
                if (delta_time > 0.0f) {
                    Vec2 velocity = {(chosen->position.x - retained->position.x) / delta_time,
                                     (chosen->position.y - retained->position.y) / delta_time};
                    float prediction_time = settings.prediction_time;
                    Vec2 predicted_pos = {chosen->position.x + velocity.x * prediction_time,
                                          chosen->position.y + velocity.y * prediction_time};
                    Vec2 error = {predicted_pos.x - mouseVec.x, predicted_pos.y - mouseVec.y};
                    auto delta=aim_delta(error,settings,dt);
                    if(delta.x==0&&delta.y==0)remainder={};
                    const LONG x=static_cast<LONG>(std::trunc(delta.x+remainder.x)),y=static_cast<LONG>(std::trunc(delta.y+remainder.y));
                    remainder={delta.x+remainder.x-static_cast<float>(x),delta.y+remainder.y-static_cast<float>(y)};
                    if((x||y)&&GetForegroundWindow()==game){INPUT input{};input.type=INPUT_MOUSE;input.mi.dx=x;input.mi.dy=y;input.mi.dwFlags=MOUSEEVENTF_MOVE;
                        if(SendInput(1,&input,sizeof(input))!=1){settings.aim=false;ui.notice="SendInput failed; aim disabled (integrity level/UIPI may block input)";}
                        else ++input_events;}
                } else {
                    // fallback to no prediction
                    auto delta=aim_delta({chosen->position.x-mouseVec.x,chosen->position.y-mouseVec.y},settings,dt);
                    if(delta.x==0&&delta.y==0)remainder={};
                    const LONG x=static_cast<LONG>(std::trunc(delta.x+remainder.x)),y=static_cast<LONG>(std::trunc(delta.y+remainder.y));
                    remainder={delta.x+remainder.x-static_cast<float>(x),delta.y+remainder.y-static_cast<float>(y)};
                    if((x||y)&&GetForegroundWindow()==game){INPUT input{};input.type=INPUT_MOUSE;input.mi.dx=x;input.mi.dy=y;input.mi.dwFlags=MOUSEEVENTF_MOVE;
                        if(SendInput(1,&input,sizeof(input))!=1){settings.aim=false;ui.notice="SendInput failed; aim disabled (integrity level/UIPI may block input)";}
                        else ++input_events;}
                }
            } else {
                auto delta=aim_delta({chosen->position.x-mouseVec.x,chosen->position.y-mouseVec.y},settings,dt);
                if(delta.x==0&&delta.y==0)remainder={};
                const LONG x=static_cast<LONG>(std::trunc(delta.x+remainder.x)),y=static_cast<LONG>(std::trunc(delta.y+remainder.y));
                remainder={delta.x+remainder.x-static_cast<float>(x),delta.y+remainder.y-static_cast<float>(y)};
                if((x||y)&&GetForegroundWindow()==game){INPUT input{};input.type=INPUT_MOUSE;input.mi.dx=x;input.mi.dy=y;input.mi.dwFlags=MOUSEEVENTF_MOVE;
                    if(SendInput(1,&input,sizeof(input))!=1){settings.aim=false;ui.notice="SendInput failed; aim disabled (integrity level/UIPI may block input)";}
                    else ++input_events;}
            }
            // Update retained target and last_target_time for next frame
            if (chosen) {
                // Set chosen's previous fields based on retained (if any)
                if (retained) {
                    chosen->previous_position = retained->position;
                    chosen->previous_time = last_target_time;
                    chosen->has_previous = true;
                } else {
                    chosen->has_previous = false;
                }
                retained = chosen;
                last_target_time = now;
            }
        } else {
            retained.reset();remainder={};
        }
        const bool render_overlay=active||(opt.benchmark&&has_area&&fresh);
        if(render_overlay){overlay.resize(static_cast<UINT>(viewport.x),static_cast<UINT>(viewport.y));overlay.frame();
            auto visual=settings;if(opt.benchmark){visual.esp=true;visual.skeleton=true;visual.tracers=true;}
            draw_overlay(visual,last,viewport,true,retained);overlay.present(menu_open?0:1);}
        if(menu_open&&!IsIconic(menu.window)){RECT r{};GetClientRect(menu.window,&r);menu.resize(static_cast<UINT>(r.right),static_cast<UINT>(r.bottom));
            const UINT dpi=GetDpiForWindow(menu.window);
            if(dpi!=menu.dpi){menu.current();ImGui::GetIO().FontGlobalScale=static_cast<float>(dpi)/static_cast<float>(menu.dpi)*ImGui::GetIO().FontGlobalScale;
                apply_theme(settings.dark_mode);ImGui::GetStyle().ScaleAllSizes(static_cast<float>(dpi)/96.0f);menu.dpi=dpi;}
            ui.dpi_scale=static_cast<float>(menu.dpi)/96.0f;
            update_avatar(last,now);
            const auto update=updates.status();ui.update_busy=update.busy;ui.update_available=update.available;ui.update_staged=update.staged;
            ui.update_message=update.message;ui.update_version=update.version;
            menu.frame();draw_menu(settings,last,ui,dt>0?1.0/static_cast<double>(dt):0,elapsed);menu.present(1);
            if(!health_confirmed){acknowledge_update_health();health_confirmed=true;}
            if(ui.update_check){updates.check();ui.update_check=false;}
            if(ui.update_download){updates.download();ui.update_download=false;}
            if(ui.update_install){std::string error;if(updates.install(error))ui.quit=true;else MessageBoxA(menu.window,error.c_str(),"Noctra update",MB_OK|MB_ICONERROR);ui.update_install=false;}
            if(ui.preview_status=="Account avatar image")++avatar_frames;
            if(ui.minimize){ShowWindow(menu.window,SW_MINIMIZE);ui.minimize=false;}}
        else if(!render_overlay)std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if(was_active&&!active){retained.reset();remainder={};}was_active=active;
        if(opt.smoke||opt.benchmark){if(elapsed>1){frames.push_back(std::chrono::duration<double,std::milli>(Clock::now()-now).count());reads.push_back(last.read_ms);if(fresh)++ready_frames;}}
    }
    if(opt.smoke||opt.benchmark){const double elapsed=std::chrono::duration<double>(Clock::now()-start).count();
        write_diagnostic(last,opt.output);std::ofstream out(opt.output,std::ios::app);if(!out)throw std::runtime_error("Cannot append test result");
        std::sort(frames.begin(),frames.end());std::sort(reads.begin(),reads.end());
        const auto percentile=[](const std::vector<double>& values,double fraction){return values.empty()?0:values[static_cast<std::size_t>(fraction*static_cast<double>(values.size()-1))];};
        out<<std::fixed<<std::setprecision(3)<<"elapsed_seconds="<<elapsed<<"\nrender_samples="<<frames.size()
           <<"\nframe_median_ms="<<percentile(frames,0.5)<<"\nframe_p95_ms="<<percentile(frames,0.95)
           <<"\nread_median_ms="<<percentile(reads,0.5)<<"\nread_p95_ms="<<percentile(reads,0.95)
           <<"\nprocess_cpu_one_core_percent="<<static_cast<double>(cpu_time()-cpu_start)/1e7/elapsed*100
           <<"\nready_frames="<<ready_frames<<"\ninput_events="<<input_events<<"\nmenu_tabs_mask="<<smoke_mask
           <<"\navatar_render_frames="<<avatar_frames<<"\navatar_error="<<avatar_error<<'\n';
        out.flush();if(!out)throw std::runtime_error("Test result write failed");
        std::cout<<std::fixed<<std::setprecision(3)<<"elapsed_seconds="<<elapsed<<"\nrender_samples="<<frames.size()
            <<"\nframe_median_ms="<<percentile(frames,0.5)<<"\nframe_p95_ms="<<percentile(frames,0.95)
            <<"\nread_median_ms="<<percentile(reads,0.5)<<"\nread_p95_ms="<<percentile(reads,0.95)
            <<"\nprocess_cpu_one_core_percent="<<static_cast<double>(cpu_time()-cpu_start)/1e7/elapsed*100
            <<"\nready_frames="<<ready_frames<<"\ninput_events="<<input_events<<"\nmenu_tabs_mask="<<smoke_mask
            <<"\navatar_render_frames="<<avatar_frames<<"\navatar_error="<<avatar_error<<std::endl;
        if(opt.smoke&&(smoke_mask!=15||frames.empty()))return 2;
        if(opt.benchmark&&ready_frames==0)return 2;
    }
    return 0;
}
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    try{return noctra::execute(instance);}catch(const std::exception& e){
        std::filesystem::create_directories("artifacts");std::ofstream("artifacts/fatal.txt")<<e.what();
        MessageBoxA(nullptr,e.what(),"NOCTRA error",MB_OK|MB_ICONERROR);return 1;}
}
