// verify.cpp — built-in self-test: injection state, AGENTS integrity, e2e codex check
#include "verify.h"

#include "config.h"
#include "proxy.h"
#include "resources.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#pragma comment(lib, "advapi32.lib")
#endif

namespace fs = std::filesystem;

namespace helmx {

namespace {

int g_failures = 0;

void check(bool ok, const char* name, const char* detail, std::string& report) {
    char line[1024];
    std::snprintf(line, sizeof(line), "  [%s] %s %s\n", ok ? "通过" : "失败", name, detail);
    report += line;
    if (!ok) g_failures++;
}

std::string read_file_str(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Run a command, capture stdout. Returns false if spawn failed.
bool run_capture(const std::string& cmd, std::string& out, int timeout_sec = 120) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr, write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) return false;
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};
    std::string full_cmd = cmd;
    BOOL ok = CreateProcessA(
        nullptr, full_cmd.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(write_pipe);
    if (!ok) {
        CloseHandle(read_pipe);
        return false;
    }

    // read with timeout
    out.clear();
    char buf[4096];
    DWORD deadline = GetTickCount() + (DWORD)timeout_sec * 1000;
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD n = 0;
            if (ReadFile(read_pipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
                out.append(buf, n);
                continue;
            }
        }
        DWORD rc = WaitForSingleObject(pi.hProcess, 50);
        if (rc == WAIT_TIMEOUT) {
            if (GetTickCount() > deadline) {
                TerminateProcess(pi.hProcess, 1);
                break;
            }
            continue;
        }
        // drain remaining
        for (;;) {
            DWORD avail2 = 0;
            if (PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &avail2, nullptr) && avail2 > 0) {
                DWORD n = 0;
                if (ReadFile(read_pipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
                    out.append(buf, n);
                    continue;
                }
            }
            break;
        }
        break;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(read_pipe);
    return true;
#else
    (void)cmd; (void)out; (void)timeout_sec;
    return false;
#endif
}

}  // namespace

// Shared: run `codex exec --skip-git-repo-check helmx` and capture output.
// codex on Windows is a .cmd shim (npm); CreateProcess cannot run it
// directly, so route through cmd /c.
bool codex_exec_capture(std::string& out, int timeout_sec) {
    std::string cmd = "cmd /c \"codex exec --skip-git-repo-check helmx 2>&1\"";
    return run_capture(cmd, out, timeout_sec);
}

int run_verify(bool e2e, std::string& report) {
    report.clear();
    g_failures = 0;

    report += "helm-x 自检\n";
    report += "=============\n";

    // 1. codex home
    std::string home = find_codex_home();
    check(!home.empty(), "codex 主目录", home.c_str(), report);
    if (home.empty()) {
        char line[128];
        std::snprintf(line, sizeof(line), "\n%d 项检查失败\n", g_failures);
        report += line;
        return 1;
    }

    // 2. config.toml exists
    fs::path cfg = fs::path(home) / "config.toml";
    check(fs::exists(cfg), "config.toml 存在", cfg.string().c_str(), report);

    // 3. provider 配置有效（代理模式 / apply 模式均可通过）
    std::string provider, base_url;
    bool provider_ok = read_active_provider(home, provider, base_url) && !base_url.empty();
    std::string provider_detail = provider_ok ? (provider + " -> " + base_url) : "缺少有效的 model_provider/base_url";
    check(provider_ok, "provider 配置有效", provider_detail.c_str(), report);

    // 4. embedded resources non-empty (AGENTS.md not deployed as file — proxy injects)
    int res_count = 0;
    if (!get_resource(ResId::AgentsMd).empty()) res_count++;
    if (!get_resource(ResId::TamperRules).empty()) res_count++;
    if (!get_resource(ResId::DashboardHtml).empty()) res_count++;
    if (!get_resource(ResId::RewritePrompt).empty()) res_count++;
    bool res_ok = res_count >= 3;  // At least AgentsMd + TamperRules + DashboardHtml
    std::string res_detail = std::to_string(res_count) + "/4";
    check(res_ok, "内置资源解密", res_detail.c_str(), report);

    // 5. backup exists（apply 用 .helmx-bak；代理用 .helmx-proxy-bak）
    //    若 config 从未被 helm-x 修改过（无备份、base_url 未指向本地代理），
    //    视为安全状态，无需备份。
    bool bak_ok = fs::exists(cfg.string() + ".helmx-bak") ||
                  fs::exists(cfg.string() + ".helmx-proxy-bak");
    bool proxied = base_url.find("127.0.0.1") != std::string::npos;
    std::string bak_detail;
    if (bak_ok) bak_detail = "";
    else if (proxied) bak_detail = "config 指向本地代理但缺少备份，请运行 helmx proxy --restore";
    else bak_detail = "config 未被 helm-x 修改，无需备份";
    check(bak_ok || !proxied, "配置备份/未修改", bak_detail.c_str(), report);

    // 8. e2e
    if (e2e) {
        report += "  [....] 端到端: codex exec \"helmx\" (可能需要 1-2 分钟)...\n";
        std::fflush(stdout);
        std::string out;
        bool spawned = codex_exec_capture(out, 240);
        // Check for both old and new activation responses
        bool activated = spawned && (
            out.find("Knowing you, I still like you") != std::string::npos ||
            out.find("helm-x online") != std::string::npos ||
            out.find("v45 online") != std::string::npos
        );
        if (!spawned) {
            std::string wout;
            run_capture("cmd /c \"where codex 2>&1\"", wout, 15);
            report += "  [....] where codex -> " + wout + "\n";
        }
        check(activated, "端到端: codex 激活 (helmx)",
              activated ? "" : (spawned ? "(回复缺失)" : "(codex 启动失败)"), report);
    } else {
        report += "  [跳过] 端到端 codex 检查 (使用 --e2e 运行)\n";
    }

    report += "=============\n";
    if (g_failures == 0) {
        report += e2e ? "全部检查通过 (含端到端)\n" : "全部检查通过\n";
        return 0;
    }
    char line[128];
    std::snprintf(line, sizeof(line), "%d 项检查失败\n", g_failures);
    report += line;
    return 1;
}

int verify_main(int argc, char** argv) {
    bool e2e = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--e2e") == 0) e2e = true;
    }
    std::string report;
    int rc = run_verify(e2e, report);
    std::printf("%s", report.c_str());
    return rc;
}

int run_zxwn(std::string& out, bool& activated) {
    out.clear();
    activated = false;
    std::string raw;
    // codex reasoning can exceed 4 min on first run; give it 6 min
    if (!codex_exec_capture(raw, 360)) {
        out = "[失败] 无法运行 codex (是否已安装? 运行 `where codex` 检查)";
        return 1;
    }
    out = raw;
    // Check for both old and new activation responses
    activated = raw.find("Knowing you, I still like you") != std::string::npos ||
                raw.find("helm-x online") != std::string::npos ||
                raw.find("v45 online") != std::string::npos;
    return activated ? 0 : 1;
}

int zxwn_cmd() {
    // 激活需要本地代理注入 AGENTS；若代理未运行（如直接命令行执行 activate），
    // 自动启动一个临时代理，激活完成后自动停止并恢复 codex 配置。
    std::thread proxy_thread;
    bool auto_proxy = false;
    if (!tcp_listening(1800)) {
        std::printf("[helm-x] 本地代理未运行，正在自动启动...\n");
        std::fflush(stdout);
        proxy_thread = std::thread([] {
            std::vector<std::string> args = {"helmx", "proxy", "--listen", "1800"};
            std::vector<char*> argv;
            for (auto& a : args) argv.push_back(a.data());
            proxy_main((int)argv.size(), argv.data());
        });
        auto_proxy = true;
        for (int i = 0; i < 50 && !tcp_listening(1800); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::printf("helm-x 正在向 codex 发送激活词...\n");
    std::fflush(stdout);

    std::string out;
    bool activated = false;
    int rc = run_zxwn(out, activated);
    std::printf("%s\n", out.c_str());

    if (auto_proxy) {
        proxy_stop();  // 让代理退出 accept 循环并恢复 codex 配置
        if (proxy_thread.joinable()) proxy_thread.join();  // 等恢复完成再退出
    }

    if (activated) {
        std::printf("[OK] 激活已确认\n");
        return 0;
    }
    std::fprintf(stderr, "[警告] 未在 codex 回复中检测到激活词\n");
    return rc;
}

}  // namespace helmx
