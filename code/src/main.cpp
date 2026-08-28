// helm-x — Codex CLI environment control tool (C++17, single binary)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#endif

#include "config.h"
#include "log.h"
#include "proxy.h"
#include "resources.h"
#include "ui.h"
#include "verify.h"
#include "watch.h"

namespace {

#ifdef _WIN32
constexpr wchar_t kStartupKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kStartupValue[] = L"helm-x";

std::wstring to_wide(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
                                   static_cast<int>(value.size()), nullptr, 0);
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (size <= 0) {
        code_page = CP_ACP;
        flags = 0;
        size = MultiByteToWideChar(code_page, flags, value.c_str(),
                                   static_cast<int>(value.size()), nullptr, 0);
    }
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(code_page, flags, value.c_str(), static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}

std::wstring executable_path() {
    std::vector<wchar_t> path(32768);
    DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (size == 0 || size >= path.size()) return {};
    return std::wstring(path.data(), size);
}

bool set_autostart(bool enabled) {
    HKEY key = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kStartupKey, 0, nullptr, 0,
                              KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) return false;

    if (enabled) {
        const std::wstring path = executable_path();
        if (path.empty()) {
            RegCloseKey(key);
            return false;
        }
        const std::wstring command = L"\"" + path + L"\" --autostart";
        rc = RegSetValueExW(key, kStartupValue, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(command.c_str()),
                            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kStartupValue);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }

    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

bool autostart_enabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStartupKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    LONG rc = RegQueryValueExW(key, kStartupValue, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

void hide_console() {
    HWND console = GetConsoleWindow();
    if (console) ShowWindow(console, SW_HIDE);
}

bool start_restore_guard(const std::string& home, const std::string& upstream) {
    const std::wstring exe = executable_path();
    const std::wstring wide_home = to_wide(home);
    const std::wstring wide_upstream = to_wide(upstream);
    if (exe.empty() || wide_home.empty()) return false;

    std::wstring command = L"\"" + exe + L"\" --restore-guard " +
                           std::to_wstring(GetCurrentProcessId()) + L" \"" +
                           wide_home + L"\" \"" + wide_upstream + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr,
                             &startup, &process);
    if (!ok) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

int run_restore_guard(DWORD parent_pid, const std::string& home,
                      const std::string& upstream) {
    hide_console();
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parent_pid);
    if (parent) {
        WaitForSingleObject(parent, INFINITE);
        CloseHandle(parent);
    }

    for (int attempt = 0; attempt < 50; ++attempt) {
        std::string provider;
        std::string base_url;
        if (helmx::read_active_provider(home, provider, base_url) &&
            base_url.find("127.0.0.1") == std::string::npos) return 0;
        if (helmx::restore_config_proxy(home, upstream)) return 0;
        Sleep(100);
    }
    return 1;
}
#else
bool set_autostart(bool) { return false; }
bool autostart_enabled() { return false; }
void hide_console() {}
bool start_restore_guard(const std::string&, const std::string&) { return false; }
#endif

}  // namespace

// Double-click launch: run proxy + UI in same process, open browser.
// 让控制台按 UTF-8 输出，避免中文/特殊符号在 GBK 控制台下乱码（鈥?）。
static void setup_console_utf8() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

static void launch_dashboard(bool open_browser) {
    const char* port_env = std::getenv("HELMX_PORT");
    int port = 8090;
    if (port_env && *port_env) {
        int p = std::atoi(port_env);
        if (p > 0) port = p;
    }
    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    const std::string home = helmx::find_codex_home();
    const std::string upstream = home.empty() ? "" : helmx::read_relay_url(home);
    if (!home.empty() && !start_restore_guard(home, upstream)) {
        helmx::log_error("启动: 配置恢复守护进程启动失败");
    }

    std::printf("==============================================\n");
    std::printf("  helm-x  -  代理 + Web 控制台\n");
    std::printf("  代理    : http://127.0.0.1:1800\n");
    std::printf("  界面    : http://127.0.0.1:%d\n", port);
    std::printf("  日志    : %%USERPROFILE%%\\.codex\\helmx.log\n");
    std::printf("  关闭此窗口将停止所有服务。\n");
    std::printf("==============================================\n");
    std::fflush(stdout);

    // Start UI in a background thread
    std::thread ui_thread([port]() {
        std::string port_str = std::to_string(port);
        std::vector<std::string> ui_args = {"helmx", "ui", "--port", port_str};
        std::vector<char*> ui_argv;
        for (auto& a : ui_args) ui_argv.push_back(a.data());
        helmx::ui_main((int)ui_argv.size(), ui_argv.data());
    });
    ui_thread.detach();

    // Start the activation self-check as soon as the proxy begins listening.
    helmx::ui_auto_activate();

    // Wait until the UI is accepting connections before opening the browser.
#ifdef _WIN32
    WSADATA wsa{};
    bool ui_ready = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    if (ui_ready) {
        ui_ready = false;
        for (int attempt = 0; attempt < 50 && !ui_ready; ++attempt) {
            SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s != INVALID_SOCKET) {
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                addr.sin_port = htons((u_short)port);
                ui_ready = ::connect(s, (sockaddr*)&addr, sizeof(addr)) == 0;
                ::closesocket(s);
            }
            if (!ui_ready) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        WSACleanup();
    }
#else
    bool ui_ready = true;
#endif

    // Open browser
#ifdef _WIN32
    if (!ui_ready) {
        std::fprintf(stderr, "[helm-x] 界面启动失败: %s\n", url.c_str());
    } else if (open_browser) {
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
#endif

    // Run proxy in main thread (blocks until shutdown)
    std::vector<std::string> proxy_args = {"helmx", "proxy", "--listen", "1800"};
    std::vector<char*> proxy_argv;
    for (auto& a : proxy_args) proxy_argv.push_back(a.data());
    helmx::proxy_main((int)proxy_argv.size(), proxy_argv.data());
}

static void usage() {
    std::printf(
        "helm-x - Codex 环境控制工具\n"
        "\n"
        "用法: helmx <命令> [参数]\n"
        "\n"
        "命令:\n"
        "  apply              部署 AGENTS.md + 配置注入\n"
        "  verify             自检注入状态 [--e2e 运行 codex 检查]\n"
        "  activate           通过 codex 发送激活词 'helmx'\n"
        "  startup on|off     开启/关闭 Windows 登录自启\n"
        "  startup status     查看 Windows 登录自启状态\n"
        "  ui                 Web 仪表盘 (状态 / 规则 / 操作)\n"
        "  watch              自愈守护进程 (校验 + 恢复)\n"
        "  proxy              篡改代理 (HTTP MITM 注入 + 重写)\n"
        "  remove             卸载并恢复备份\n"
        "\n");
}

int main(int argc, char** argv) {
    setup_console_utf8();
    if (argc < 2) {
        // Double-click keeps helm-x available after the next Windows login.
        if (!autostart_enabled()) {
            if (set_autostart(true)) helmx::log_info("启动: 已注册 Windows 登录自启");
            else helmx::log_error("启动: 注册 Windows 登录自启失败");
        }
        launch_dashboard(true);
        helmx::log_info("启动: 双击请求打开仪表盘");
        return 0;
    }

    const std::string cmd = argv[1];

    if (cmd == "--restore-guard") {
#ifdef _WIN32
        if (argc < 5) return 1;
        DWORD parent_pid = static_cast<DWORD>(std::strtoul(argv[2], nullptr, 10));
        return parent_pid == 0 ? 1 : run_restore_guard(parent_pid, argv[3], argv[4]);
#else
        return 1;
#endif
    } else if (cmd == "--autostart") {
        hide_console();
        helmx::log_info("启动: Windows 登录自启");
        launch_dashboard(false);
        return 0;
    } else if (cmd == "startup") {
        const std::string action = argc > 2 ? argv[2] : "status";
        if (action == "on") {
            bool ok = set_autostart(true);
            std::printf("%s Windows 登录自启\n", ok ? "[OK] 已开启" : "[失败] 无法开启");
            return ok ? 0 : 1;
        }
        if (action == "off") {
            bool ok = set_autostart(false);
            std::printf("%s Windows 登录自启\n", ok ? "[OK] 已关闭" : "[失败] 无法关闭");
            return ok ? 0 : 1;
        }
        if (action == "status") {
            bool enabled = autostart_enabled();
            std::printf("Windows 登录自启: %s\n", enabled ? "已开启" : "未开启");
            return enabled ? 0 : 1;
        }
        std::fprintf(stderr, "用法: helmx startup on|off|status\n");
        return 1;
    } else if (cmd == "apply") {
        return helmx::apply();
    } else if (cmd == "verify") {
        return helmx::verify_main(argc, argv);
    } else if (cmd == "activate" || cmd == "zxwn") {
        return helmx::zxwn_cmd();
    } else if (cmd == "ui") {
        return helmx::ui_main(argc, argv);
    } else if (cmd == "watch") {
        return helmx::watch(argc > 2 ? std::atoi(argv[2]) : 60);
    } else if (cmd == "proxy") {
        bool restore_only = false;
        std::string upstream;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--restore") == 0) restore_only = true;
            if (std::strcmp(argv[i], "--upstream") == 0 && i + 1 < argc) {
                upstream = argv[++i];
            }
        }
        if (!restore_only) {
            const std::string home = helmx::find_codex_home();
            if (upstream.empty() && !home.empty()) upstream = helmx::read_relay_url(home);
            if (!home.empty() && !start_restore_guard(home, upstream)) {
                helmx::log_error("启动: 配置恢复守护进程启动失败");
            }
        }
        return helmx::proxy_main(argc, argv);
    } else if (cmd == "remove") {
        return helmx::remove();
    } else if (cmd == "help" || cmd == "-h" || cmd == "--help") {
        usage();
        return 0;
    }

    std::fprintf(stderr, "helm-x: 未知命令 '%s'\n\n", cmd.c_str());
    usage();
    return 1;
}
