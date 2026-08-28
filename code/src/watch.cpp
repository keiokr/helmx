// watch.cpp — self-healing daemon (controllable background service)
#include "watch.h"

#include "config.h"
#include "log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace helmx {

namespace {
std::atomic<bool> g_watch_running{false};
std::atomic<int> g_watch_restores{0};
std::atomic<long long> g_last_restore_ts{0};
std::thread g_watch_thread;

// verify + restore one pass. Returns true if injection was intact.
bool watch_pass(const std::string& home) {
    if (verify_injection(home)) return true;
    std::printf("[helm-x] 注入被破坏, 正在恢复...\n");
    std::fflush(stdout);
    log_info("守护: 注入被破坏, 正在恢复");
    inject_config(home);
    if (verify_injection(home)) {
        g_watch_restores++;
        g_last_restore_ts = (long long)std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        std::printf("[helm-x] 已恢复\n");
        std::fflush(stdout);
        log_info("守护: 已恢复 (累计 " + std::to_string(g_watch_restores.load()) + " 次)");
    } else {
        std::fprintf(stderr, "[helm-x] 恢复失败\n");
        log_error("守护: 恢复失败");
    }
    return false;
}
}  // namespace

void watch_start(int interval_sec) {
    if (interval_sec < 5) interval_sec = 5;
    bool expected = false;
    if (!g_watch_running.compare_exchange_strong(expected, true)) return;
    log_info("守护: 启动 (间隔 " + std::to_string(interval_sec) + " 秒)");
    g_watch_thread = std::thread([interval_sec] {
        std::string home = find_codex_home();
        if (home.empty()) {
            std::fprintf(stderr, "[helm-x] 未找到 codex 主目录\n");
            log_error("守护: 未找到 codex 主目录");
            g_watch_running = false;
            return;
        }
        std::printf("[helm-x] 守护已启动 (间隔 %d 秒)\n", interval_sec);
        std::fflush(stdout);
        while (g_watch_running.load()) {
            watch_pass(home);
            for (int i = 0; i < interval_sec && g_watch_running.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        std::printf("[helm-x] 守护已停止\n");
        std::fflush(stdout);
        log_info("守护: 已停止");
    });
    g_watch_thread.detach();
}

void watch_stop() {
    if (g_watch_running.load()) {
        log_info("守护: 收到停止请求");
    }
    g_watch_running = false;
}

bool watch_running() {
    return g_watch_running.load();
}

int watch_restores() {
    return g_watch_restores.load();
}

long long watch_last_restore_ts() {
    return g_last_restore_ts.load();
}

int watch(int interval_sec) {
    if (interval_sec < 5) interval_sec = 5;
    std::printf("[helm-x] 守护已启动 (间隔 %d 秒) - Ctrl+C 停止\n", interval_sec);

    std::string home = find_codex_home();
    if (home.empty()) {
        std::fprintf(stderr, "[helm-x] 未找到 codex 主目录\n");
        return 1;
    }

    while (true) {
        watch_pass(home);
        std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
    }
    return 0;
}

}  // namespace helmx
