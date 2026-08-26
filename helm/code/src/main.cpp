// helm-x — Codex CLI environment control tool (C++17, single binary)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>

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

// Double-click launch: run proxy + UI in same process, open browser.
// 让控制台按 UTF-8 输出，避免中文/特殊符号在 GBK 控制台下乱码（鈥?）。
static void setup_console_utf8() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

static void launch_dashboard() {
    const char* port_env = std::getenv("HELMX_PORT");
    int port = 8090;
    if (port_env && *port_env) {
        int p = std::atoi(port_env);
        if (p > 0) port = p;
    }
    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";

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

    // 打开软件自动激活：异步发送激活词，让 codex 会话带上 AGENTS 角色
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
    if (ui_ready) ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else std::fprintf(stderr, "[helm-x] 界面启动失败: %s\n", url.c_str());
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
        "  ui                 Web 仪表盘 (状态 / 规则 / 操作)\n"
        "  watch              自愈守护进程 (校验 + 恢复)\n"
        "  proxy              篡改代理 (HTTP MITM 注入 + 重写)\n"
        "  remove             卸载并恢复备份\n"
        "\n");
}

int main(int argc, char** argv) {
    setup_console_utf8();
    if (argc < 2) {
        // double-click: open dashboard in browser
        launch_dashboard();
        helmx::log_info("启动: 双击请求打开仪表盘");
        return 0;
    }

    const std::string cmd = argv[1];

    if (cmd == "apply") {
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
