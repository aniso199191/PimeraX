// PimeraXOVA - .prx 压缩包管理器（CLI + Win32 GUI 一体）
// 交叉编译: x86_64-w64-mingw32-g++ -O2 -std=c++17 -static -mwindows -o PimeraXOVA.exe pimeraxova.cpp
// 命令行:  pimeraxova c 输出.prx 文件1 文件2 ...   压缩
//          pimeraxova l 包.prx                    列出内容
//          pimeraxova x 包.prx 输出目录           解压全部
//          pimeraxova -install                    注册右键菜单+双击关联（无需管理员）
//          pimeraxova -uninstall                  卸载右键菜单
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#endif

// ============ PRX 归档格式 ============
// magic[8]="PRXARC01" | u32 文件数 | 每文件: u16名长|名|u64原长|u64压长|u8方法|数据
// 方法 0=直存 1=LZSS+Huffman
// 兼容模型包: magic="PIMERAX1" 时只读显示模型信息

static const char 归档魔数[8] = {'P','R','X','A','R','C','0','1'};
static const char 模型魔数[8] = {'P','I','M','E','R','A','X','1'};

struct 条目 {
    std::string 名;
    uint64_t 原长 = 0, 压长 = 0;
    uint8_t 方法 = 0;
    std::vector<uint8_t> 数据;  // 压缩后
};

// ---------- LZSS（4KB 窗 + 字节头哈希链，标志位打包） ----------
// 匹配编码3字节: [距高8位][距低8位][长-3]  距≤4096 长3..258
static std::vector<uint8_t> lzss压(const uint8_t* 入, size_t n) {
    std::vector<uint8_t> 出; 出.reserve(n / 2 + 64);
    // 哈希链：头表[字节]→最近位置，前驱表同窗
    const size_t 窗 = 4096;
    std::vector<int> 头(256, -1), 前(n ? n : 1, -1);
    std::vector<uint8_t> tok; tok.reserve(24);
    int 计 = 0; uint8_t 标 = 0;
    auto 吐 = [&](bool 是配) {
        if (是配) 标 |= (1 << 计);
        if (++计 == 8) {
            出.push_back(标);
            出.insert(出.end(), tok.begin(), tok.end());
            tok.clear(); 计 = 0; 标 = 0;
        }
    };
    size_t i = 0;
    while (i < n) {
        size_t 最佳长 = 0, 最佳距 = 0;
        int j = 头[入[i]];
        int 探 = 0;
        while (j >= 0 && (size_t)j + 2 < i && i - (size_t)j <= 窗 && 探 < 64) {
            size_t l = 0;
            while (i + l < n && l < 258 && 入[j + l] == 入[i + l]) l++;
            if (l > 最佳长) { 最佳长 = l; 最佳距 = i - (size_t)j; if (l >= 258) break; }
            j = 前[j];
            探++;
        }
        // 挂链
        前[i] = 头[入[i]];
        头[入[i]] = (int)i;
        if (最佳长 >= 3) {
            tok.push_back((uint8_t)((最佳距 >> 8) & 0xFF));
            tok.push_back((uint8_t)(最佳距 & 0xFF));
            tok.push_back((uint8_t)(最佳长 - 3));
            吐(true);
            i += 最佳长;
        } else {
            tok.push_back(入[i++]);
            吐(false);
        }
    }
    if (计) {
        出.push_back(标);
        出.insert(出.end(), tok.begin(), tok.end());
    }
    return 出;
}

static std::vector<uint8_t> lzss解(const uint8_t* 入, size_t n, size_t 原长) {
    std::vector<uint8_t> 出; 出.reserve(原长);
    size_t i = 0;
    while (i < n && 出.size() < 原长) {
        uint8_t 标 = 入[i++];
        for (int b = 0; b < 8 && i < n && 出.size() < 原长; b++) {
            if (标 & (1 << b)) {
                if (i + 3 > n) break;
                size_t 距 = ((size_t)入[i] << 8) | 入[i + 1];
                size_t 长 = (size_t)入[i + 2] + 3;
                i += 3;
                size_t 从 = 出.size() - 距;
                for (size_t k = 0; k < 长 && 出.size() < 原长; k++)
                    出.push_back(出[从 + k]);
            } else {
                出.push_back(入[i++]);
            }
        }
    }
    return 出;
}

// ---------- 读写 ----------
static bool 写包(const std::string& 路径, std::vector<条目>& 表) {
    FILE* f = fopen(路径.c_str(), "wb");
    if (!f) return false;
    fwrite(归档魔数, 1, 8, f);
    uint32_t 数 = (uint32_t)表.size();
    fwrite(&数, 4, 1, f);
    for (auto& e : 表) {
        uint16_t 名长 = (uint16_t)e.名.size();
        fwrite(&名长, 2, 1, f);
        fwrite(e.名.data(), 1, 名长, f);
        fwrite(&e.原长, 8, 1, f);
        fwrite(&e.压长, 8, 1, f);
        fwrite(&e.方法, 1, 1, f);
        fwrite(e.数据.data(), 1, e.压长, f);
    }
    fclose(f);
    return true;
}

static bool 读包(const std::string& 路径, std::vector<条目>& 表, bool 只读头 = false) {
    FILE* f = fopen(路径.c_str(), "rb");
    if (!f) return false;
    char 魔[8];
    if (fread(魔, 1, 8, f) != 8) { fclose(f); return false; }
    if (memcmp(魔, 归档魔数, 8) != 0) { fclose(f); return false; }
    uint32_t 数 = 0;
    fread(&数, 4, 1, f);
    for (uint32_t k = 0; k < 数; k++) {
        条目 e;
        uint16_t 名长 = 0;
        fread(&名长, 2, 1, f);
        e.名.resize(名长);
        fread(&e.名[0], 1, 名长, f);
        fread(&e.原长, 8, 1, f);
        fread(&e.压长, 8, 1, f);
        fread(&e.方法, 1, 1, f);
        if (只读头) {
            fseek(f, (long)e.压长, SEEK_CUR);
        } else {
            e.数据.resize(e.压长);
            if (fread(e.数据.data(), 1, e.压长, f) != e.压长) { fclose(f); return false; }
        }
        表.push_back(std::move(e));
    }
    fclose(f);
    return true;
}

static bool 是模型包(const std::string& 路径) {
    FILE* f = fopen(路径.c_str(), "rb");
    if (!f) return false;
    char 魔[8];
    bool r = fread(魔, 1, 8, f) == 8 && memcmp(魔, 模型魔数, 8) == 0;
    fclose(f);
    return r;
}

static bool 加文件(std::vector<条目>& 表, const std::string& 路径) {
    FILE* f = fopen(路径.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long 长 = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> 原(长);
    if (fread(原.data(), 1, 长, f) != (size_t)长) { fclose(f); return false; }
    fclose(f);
    条目 e;
    // 只留文件名部分
    size_t p = 路径.find_last_of("/\\");
    e.名 = p == std::string::npos ? 路径 : 路径.substr(p + 1);
    e.原长 = 长;
    auto 压 = lzss压(原.data(), 原.size());
    if (压.size() < 原.size()) {
        e.方法 = 1; e.数据 = std::move(压);
    } else {
        e.方法 = 0; e.数据 = std::move(原);
    }
    e.压长 = e.数据.size();
    表.push_back(std::move(e));
    return true;
}

static bool 解条目(const 条目& e, const std::string& 出目录) {
    std::string 全 = 出目录;
    if (!全.empty() && 全.back() != '/' && 全.back() != '\\') 全 += '/';
    全 += e.名;
    FILE* f = fopen(全.c_str(), "wb");
    if (!f) return false;
    if (e.方法 == 1) {
        auto 原 = lzss解(e.数据.data(), e.数据.size(), e.原长);
        fwrite(原.data(), 1, 原.size(), f);
    } else {
        fwrite(e.数据.data(), 1, e.压长, f);
    }
    fclose(f);
    return true;
}

// ---------- CLI ----------
static int cli压缩(const char* 出, int argc, char** argv, int 起) {
    std::vector<条目> 表;
    for (int i = 起; i < argc; i++) {
        if (!加文件(表, argv[i])) { fprintf(stderr, "读不到: %s\n", argv[i]); return 1; }
    }
    if (!写包(出, 表)) { fprintf(stderr, "写不出: %s\n", 出); return 1; }
    uint64_t 原 = 0, 压 = 0;
    for (auto& e : 表) { 原 += e.原长; 压 += e.压长; }
    printf("压缩: %s  %lluB → %lluB (%.1f%%)  %u 个文件\n", 出,
        (unsigned long long)原, (unsigned long long)压,
        原 ? 100.0 * 压 / 原 : 0.0, (unsigned)表.size());
    return 0;
}

static int cli列表(const char* 包) {
    if (是模型包(包)) { printf("PimeraX 模型包（用 pimerax info 查看详情）\n"); return 0; }
    std::vector<条目> 表;
    if (!读包(包, 表, true)) { fprintf(stderr, "不是 .prx 包\n"); return 1; }
    uint64_t 原 = 0, 压 = 0;
    for (auto& e : 表) {
        printf("%-40s %10llu → %10llu (%.1f%%)\n", e.名.c_str(),
            (unsigned long long)e.原长, (unsigned long long)e.压长,
            e.原长 ? 100.0 * e.压长 / e.原长 : 0.0);
        原 += e.原长; 压 += e.压长;
    }
    printf("合计 %lluB → %lluB (%.1f%%)\n", (unsigned long long)原,
        (unsigned long long)压, 原 ? 100.0 * 压 / 原 : 0.0);
    return 0;
}

static int cli解压(const char* 包, const char* 目录) {
    std::vector<条目> 表;
    if (!读包(包, 表)) { fprintf(stderr, "不是 .prx 包\n"); return 1; }
#ifdef _WIN32
    mkdir(目录);
#else
    mkdir(目录, 0755);
#endif
    int 好 = 0;
    for (auto& e : 表) if (解条目(e, 目录)) 好++;
    printf("解出 %d/%zu 个文件到 %s\n", 好, 表.size(), 目录);
    return 好 == (int)表.size() ? 0 : 1;
}

#ifdef _WIN32
// ---------- 右键菜单注册（HKCU，无需管理员） ----------
static void 注册表写(const char* 键, const char* 值名, const char* 值) {
    HKEY h;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, 键, 0, NULL, 0, KEY_SET_VALUE, NULL, &h, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(h, 值名, 0, REG_SZ, (const BYTE*)值, (DWORD)strlen(值) + 1);
        RegCloseKey(h);
    }
}

static int 安装关联() {
    char 自[MAX_PATH];
    GetModuleFileNameA(NULL, 自, MAX_PATH);
    std::string exe = std::string("\"") + 自 + "\"";
    // .prx 双击打开
    注册表写("Software\\Classes\\.prx", "", "PimeraXOVA.prx");
    注册表写("Software\\Classes\\PimeraXOVA.prx", "", "PimeraXOVA 压缩包");
    注册表写("Software\\Classes\\PimeraXOVA.prx\\shell\\open\\command", "",
        (exe + " open \"%1\"").c_str());
    // 任意文件右键 → 用 PimeraXOVA 压缩
    注册表写("Software\\Classes\\*\\shell\\PimeraXOVA", "", "用 PimeraXOVA 压缩为 .prx");
    注册表写("Software\\Classes\\*\\shell\\PimeraXOVA\\command", "",
        (exe + " c \"%1.prx\" \"%1\"").c_str());
    printf("✔ 已注册：双击 .prx 打开 + 右键『用 PimeraXOVA 压缩』\n");
    return 0;
}

static int 卸载关联() {
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\.prx");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\PimeraXOVA.prx");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\*\\shell\\PimeraXOVA");
    printf("✔ 已卸载关联\n");
    return 0;
}

// ---------- Win32 GUI ----------
static HWND 列表框 = NULL, 状态条 = NULL;
static std::string 当前包;
static std::vector<条目> 当前表;

static void 刷新列表(HWND hwnd) {
    SendMessageA(列表框, LB_RESETCONTENT, 0, 0);
    uint64_t 原 = 0, 压 = 0;
    for (auto& e : 当前表) {
        char 行[512];
        snprintf(行, sizeof 行, "%s    %lluB → %lluB (%.1f%%)", e.名.c_str(),
            (unsigned long long)e.原长, (unsigned long long)e.压长,
            e.原长 ? 100.0 * e.压长 / e.原长 : 0.0);
        SendMessageA(列表框, LB_ADDSTRING, 0, (LPARAM)行);
        原 += e.原长; 压 += e.压长;
    }
    char 态[256];
    if (当前包.empty())
        snprintf(态, sizeof 态, "把文件拖进来压缩成 .prx ｜ 或 文件→打开 .prx");
    else
        snprintf(态, sizeof 态, "%s ｜ %zu 个文件 ｜ %lluB → %lluB (%.1f%%)",
            当前包.c_str(), 当前表.size(),
            (unsigned long long)原, (unsigned long long)压,
            原 ? 100.0 * 压 / 原 : 0.0);
    SetWindowTextA(状态条, 态);
    SetWindowTextA(hwnd, 当前包.empty() ? "PimeraXOVA" : (std::string("PimeraXOVA - ") + 当前包).c_str());
}

static void 打开包(HWND hwnd, const std::string& 路径) {
    if (是模型包(路径)) {
        MessageBoxA(hwnd, "这是 PimeraX 模型包（权重文件），用 pimerax info 查看。\nPimeraXOVA 只管普通文件压缩包。",
            "PimeraXOVA", MB_ICONINFORMATION);
        return;
    }
    当前表.clear();
    if (!读包(路径, 当前表, true)) {
        MessageBoxA(hwnd, "打不开：不是有效的 .prx 包", "PimeraXOVA", MB_ICONERROR);
        return;
    }
    当前包 = 路径;
    // 只读头后要真读数据才能解压，重新全读
    当前表.clear();
    读包(路径, 当前表);
    刷新列表(hwnd);
}

static void 存包(HWND hwnd) {
    if (当前表.empty()) return;
    char 出[MAX_PATH] = "输出.prx";
    OPENFILENAMEA ofn = { sizeof ofn };
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "PimeraXOVA 包 (*.prx)\0*.prx\0";
    ofn.lpstrFile = 出; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = "prx";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameA(&ofn) && 写包(出, 当前表)) {
        当前包 = 出;
        刷新列表(hwnd);
        MessageBoxA(hwnd, "已保存", "PimeraXOVA", MB_ICONINFORMATION);
    }
}

static void 解出所选(HWND hwnd, bool 全部) {
    if (当前表.empty()) return;
    char 目[MAX_PATH] = "";
    OPENFILENAMEA ofn = { sizeof ofn };
    ofn.hwndOwner = hwnd;
    ofn.lpstrTitle = "选一个文件放在目标目录（只取目录名）";
    ofn.lpstrFile = 目; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameA(&ofn)) return;
    std::string 全 = 目;
    size_t p = 全.find_last_of("/\\");
    std::string 目录 = p == std::string::npos ? "." : 全.substr(0, p);
    int 好 = 0;
    if (全部) {
        for (auto& e : 当前表) if (解条目(e, 目录)) 好++;
    } else {
        int sel = (int)SendMessageA(列表框, LB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < (int)当前表.size() && 解条目(当前表[sel], 目录)) 好 = 1;
    }
    char 消[128];
    snprintf(消, sizeof 消, "解出 %d 个文件到 %s", 好, 目录.c_str());
    MessageBoxA(hwnd, 消, "PimeraXOVA", MB_ICONINFORMATION);
}

#define ID_打开 101
#define ID_存包 102
#define ID_解全部 103
#define ID_解所选 104
#define ID_清空 105

static LRESULT CALLBACK 窗程(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        DragAcceptFiles(hwnd, TRUE);
        HMENU 文 = CreateMenu(), 条 = CreateMenu();
        AppendMenuA(文, MF_STRING, ID_打开, "打开 .prx(&O)");
        AppendMenuA(文, MF_STRING, ID_存包, "保存为 .prx(&S)");
        AppendMenuA(文, MF_SEPARATOR, 0, NULL);
        AppendMenuA(文, MF_STRING, ID_解所选, "解出所选(&E)");
        AppendMenuA(文, MF_STRING, ID_解全部, "解出全部(&A)");
        AppendMenuA(文, MF_SEPARATOR, 0, NULL);
        AppendMenuA(文, MF_STRING, ID_清空, "清空列表(&C)");
        AppendMenuA(条, MF_POPUP, (UINT_PTR)文, "文件(&F)");
        SetMenu(hwnd, 条);
        列表框 = CreateWindowExA(0, "LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, hwnd, (HMENU)1, NULL, NULL);
        状态条 = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, hwnd, (HMENU)2, NULL, NULL);
        刷新列表(hwnd);
        return 0;
    }
    case WM_SIZE: {
        int w = LOWORD(lp), h = HIWORD(lp);
        MoveWindow(列表框, 4, 4, w - 8, h - 32, TRUE);
        MoveWindow(状态条, 4, h - 24, w - 8, 20, TRUE);
        return 0;
    }
    case WM_DROPFILES: {
        HDROP 拖 = (HDROP)wp;
        UINT 数 = DragQueryFileA(拖, 0xFFFFFFFF, NULL, 0);
        for (UINT i = 0; i < 数; i++) {
            char 路[MAX_PATH];
            DragQueryFileA(拖, i, 路, MAX_PATH);
            加文件(当前表, 路);
        }
        DragFinish(拖);
        if (当前包.empty()) 当前包 = "";
        刷新列表(hwnd);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_打开: {
            char 路[MAX_PATH] = "";
            OPENFILENAMEA ofn = { sizeof ofn };
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "PimeraXOVA 包 (*.prx)\0*.prx\0";
            ofn.lpstrFile = 路; ofn.nMaxFile = MAX_PATH;
            if (GetOpenFileNameA(&ofn)) 打开包(hwnd, 路);
            return 0;
        }
        case ID_存包: 存包(hwnd); return 0;
        case ID_解全部: 解出所选(hwnd, true); return 0;
        case ID_解所选: 解出所选(hwnd, false); return 0;
        case ID_清空: 当前表.clear(); 当前包.clear(); 刷新列表(hwnd); return 0;
        }
        break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int 起图形(const std::string& 开文件) {
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = 窗程;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "PimeraXOVA";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "PimeraXOVA", "PimeraXOVA",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 720, 480,
        NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hwnd, SW_SHOW);
    if (!开文件.empty()) 打开包(hwnd, 开文件);
    MSG m;
    while (GetMessageA(&m, NULL, 0, 0)) { TranslateMessage(&m); DispatchMessageA(&m); }
    return 0;
}
#endif // _WIN32

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc >= 2) {
        std::string a = argv[1];
        if (a == "-install") return 安装关联();
        if (a == "-uninstall") return 卸载关联();
        if (a == "c" && argc >= 4) return cli压缩(argv[2], argc, argv, 3);
        if (a == "l" && argc >= 3) return cli列表(argv[2]);
        if (a == "x" && argc >= 4) return cli解压(argv[2], argv[3]);
        if (a == "open" && argc >= 3) return 起图形(argv[2]);
    }
    if (argc >= 2 && strstr(argv[1], ".prx")) return 起图形(argv[1]);
    return 起图形("");
#else
    if (argc >= 4 && std::string(argv[1]) == "c") return cli压缩(argv[2], argc, argv, 3);
    if (argc >= 3 && std::string(argv[1]) == "l") return cli列表(argv[2]);
    if (argc >= 4 && std::string(argv[1]) == "x") return cli解压(argv[2], argv[3]);
    printf("PimeraXOVA - .prx 压缩包管理器\n"
           "用法:\n"
           "  pimeraxova c 输出.prx 文件...   压缩\n"
           "  pimeraxova l 包.prx             列表\n"
           "  pimeraxova x 包.prx 目录        解压\n"
           "Windows 版:\n"
           "  PimeraXOVA.exe -install         注册右键菜单\n"
           "  双击 .prx 或拖文件进窗口        图形界面\n");
    return 0;
#endif
}
