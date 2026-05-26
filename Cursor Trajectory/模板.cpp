#include <SDL3/SDL.h>
#include <iostream>
#include <windows.h>
#include <vector>
#include <string>
#include <random>

std::mt19937 rng(std::random_device{}());
int RAND(int l, int r) {
    std::uniform_int_distribution<int> dist(l, r);
    return dist(rng);
}

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "Shell32.lib")  // 托盘需要

// ---------- 托盘相关 ----------
#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_EXIT 1001

NOTIFYICONDATAW g_nid = { 0 };
HMENU g_hTrayMenu = NULL;
bool g_wantsQuit = false;
WNDPROC g_oldWndProc = NULL;

// 托盘消息处理的回调函数（
LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TRAYICON) {
        if (lParam == WM_RBUTTONUP) {
            POINT cursorPt;
            GetCursorPos(&cursorPt);
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(g_hTrayMenu,
                TPM_RETURNCMD | TPM_NONOTIFY,
                cursorPt.x, cursorPt.y, 0, hwnd, NULL);
            if (cmd == ID_TRAY_EXIT) {
                g_wantsQuit = true;
            }
            return 0;
        }
        return 0;
    }
    return CallWindowProcW(g_oldWndProc, hwnd, msg, wParam, lParam);
}

void CreateTrayIcon(HWND hwnd) {
    g_hTrayMenu = CreatePopupMenu();
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_EXIT, L"退出");

    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"鼠标拖尾");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_hTrayMenu) DestroyMenu(g_hTrayMenu);
}

// 粒子系统
struct xy { float x, y; };

class CursorTrajectory {
public:
    std::vector<xy> Trajectoryxy;
    std::vector<SDL_Texture*> TrajectoryTexture;
    std::vector<float> TrajectoryTurn;
    std::vector<float> TrajectoryA;
    std::vector<float> TrajectoryATime;
    std::vector<float> TrajectoryR;
    std::vector<xy> Trajectoryvxy;
    std::vector<float> TrajectoryvT;
    std::vector<float> TrajectoryvR;

    void add(float x, float y, SDL_Texture* texture, float turn, float alpha, float time) {
        Trajectoryxy.push_back({ x, y });
        TrajectoryTexture.push_back(texture);
        TrajectoryTurn.push_back(turn);
        TrajectoryA.push_back(alpha);
        TrajectoryATime.push_back(time);
        TrajectoryR.push_back(40);
        Trajectoryvxy.push_back({ 0, 0 });
        TrajectoryvT.push_back(0);
        TrajectoryvR.push_back(0);
    }

    void clear() {
        Trajectoryxy.clear(); TrajectoryTexture.clear(); TrajectoryTurn.clear();
        TrajectoryA.clear(); TrajectoryATime.clear(); TrajectoryR.clear();
        Trajectoryvxy.clear(); TrajectoryvT.clear(); TrajectoryvR.clear();
    }

    void update(float dt) {
        for (int i = (int)TrajectoryA.size() - 1; i >= 0; --i) {
            TrajectoryA[i] -= (TrajectoryA[i]>50?1:5)*0.012f*TrajectoryATime[i]*dt;
            if (TrajectoryA[i] < 0) {
                TrajectoryA.erase(TrajectoryA.begin() + i);
                Trajectoryxy.erase(Trajectoryxy.begin() + i);
                TrajectoryTexture.erase(TrajectoryTexture.begin() + i);
                TrajectoryTurn.erase(TrajectoryTurn.begin() + i);
                TrajectoryATime.erase(TrajectoryATime.begin() + i);
                TrajectoryR.erase(TrajectoryR.begin() + i);
                Trajectoryvxy.erase(Trajectoryvxy.begin() + i);
                TrajectoryvT.erase(TrajectoryvT.begin() + i);
                TrajectoryvR.erase(TrajectoryvR.begin() + i);
            }
        }
    }

    void draw(SDL_Renderer* renderer) {
        for (size_t i = 0; i < TrajectoryA.size(); ++i) {
            SDL_SetTextureAlphaMod(TrajectoryTexture[i], static_cast<Uint8>(TrajectoryA[i] * 255));
            SDL_FRect dstRect = {
                Trajectoryxy[i].x - TrajectoryR[i] / 2,
                Trajectoryxy[i].y - TrajectoryR[i] / 2,
                TrajectoryR[i], TrajectoryR[i]
            };
            SDL_FPoint center = { TrajectoryR[i] / 2, TrajectoryR[i] / 2 };
            SDL_RenderTextureRotated(renderer, TrajectoryTexture[i], NULL, &dstRect,
                TrajectoryTurn[i], &center, SDL_FLIP_NONE);
        }
    }
} Cur;

// 加载函数：成功返回 true，失败返回 false
bool LoadTexturesByPrefix(SDL_Renderer* renderer, const std::string& prefix,
    std::vector<SDL_Texture*>& textures) {
    while (true) {
        std::string filename = prefix + std::to_string(textures.size() + 1) + ".png";
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, SDL_LoadPNG(filename.c_str()));

        if (tex != nullptr) {
            textures.push_back(tex);
        }
        else {
            // 加载失败，返回是否加载到至少一张
            return !textures.empty();
        }
    }
}

// 生成默认纹理的函数（和之前一样，独立出来）
SDL_Texture* CreateTRATexture(SDL_Renderer* renderer, int w, int h) {
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_Texture* oldTarget = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, tex);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    SDL_FRect r = { 0, 0, (float)w, (float)h };
    SDL_SetRenderDrawColor(renderer, 255, 195, 235, 255);
    SDL_RenderFillRect(renderer, &r);

    r = { (float)w * 0.2f, (float)h * 0.2f, (float)w * 0.6f, (float)h * 0.6f };
    SDL_SetRenderDrawColor(renderer, 255, 105, 185, 255);
    SDL_RenderFillRect(renderer, &r);

    SDL_SetRenderTarget(renderer, oldTarget);
    return tex;
}

float getsqrt(float x, float y) {
	return sqrtf(x * x + y * y);
}
int main() {
    SetProcessDPIAware();
    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int scrW = workArea.right - workArea.left;
    int scrH = workArea.bottom - workArea.top;

    SDL_Window* win = SDL_CreateWindow(
        "TrailOverlay", scrW, scrH,
        SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_BORDERLESS |
        SDL_WINDOW_TRANSPARENT | SDL_WINDOW_NOT_FOCUSABLE
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(win, nullptr);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetWindowPosition(win, workArea.left, workArea.top);

    HWND hwnd = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(win),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL
    );

    if (hwnd) {
        // 鼠标穿透
        LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
        SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT | WS_EX_LAYERED);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    g_oldWndProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)TrayWndProc);
    CreateTrayIcon(hwnd);

    // 加载纹理
    Cur.clear();
    
    std::vector<SDL_Texture*> Tra;
    if (!LoadTexturesByPrefix(renderer, "Trajectory", Tra)) 
        Tra.push_back(CreateTRATexture(renderer, 50, 50));


    bool running = true;
    SDL_Event event;
    float lastmx = 0, lastmy = 0,allx=0,ally=0;
    Uint64 lastFrameTime = SDL_GetTicks();
    while (running && !g_wantsQuit) {
        POINT cursorPt;
        GetCursorPos(&cursorPt);
        float mx = (float)cursorPt.x;
        float my = (float)cursorPt.y;
        Uint64 now = SDL_GetTicks();
        float dt = (now - lastFrameTime);
        lastFrameTime = now;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) g_wantsQuit = true;
        }

        if (mx != lastmx || my != lastmy)
            if (getsqrt(allx, ally)>107) {
                int c = getsqrt(allx, ally)/107;
                if (getsqrt(allx, ally) == 0||(allx==0&&ally==0))c = 0;
                for (int i = 0; i < min(c, 3); ++i) {
                    float dx = mx - lastmx;
                    float dy = my - lastmy;
                    // 只有在移动时才会进入这个分支，所以 dx,dy 不会同时为 0
                    float moveAngle = atan2(dy, dx) * 180.0f / 3.14159265f+90; // 弧度转度数
                    Cur.add(mx + RAND(-18, 18), my + RAND(-18, 18),
                        Tra[RAND(0, Tra.size() - 1)],
                        moveAngle + RAND(-30, 30), // 方向角 ±30° 随机偏移
                        0.88f, 0.035f);
                }
                allx = -RAND(0, 15);
                ally = -RAND(0, 15);
            }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        Cur.update(dt);
        Cur.draw(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
		allx += abs(lastmx - mx);
        ally += abs(lastmy - my);
        lastmx = mx;
        lastmy = my;
    }

    // ====== 清理 ======
    for (SDL_Texture* tex : Tra) {
        if (tex) SDL_DestroyTexture(tex);
    }
    Tra.clear();
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)g_oldWndProc);
    RemoveTrayIcon();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}