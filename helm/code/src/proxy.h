// proxy.h — HTTP MITM tamper proxy (WinHTTP upstream, SSE-safe)
#pragma once
#include <string>
namespace helmx {

// proxy --listen <port> --upstream <relay-url>
//   Local mapping: codex -> 127.0.0.1:port -> upstream relay.
//   Injects embedded AGENTS into requests, tamper-rewrites refusals.
//   Forcing stream=false avoids the SSE-stall bug of the Python original.
int proxy_main(int argc, char** argv);

// Get the relay URL that the proxy resolved at startup.
// Returns empty string if proxy hasn't started yet.
std::string get_relay_url();

// 检查 127.0.0.1:port 是否有进程在监听（用于激活前确认代理已就绪）。
bool tcp_listening(int port);

// 请求代理优雅退出（accept 循环退出后会恢复 codex 配置）。
void proxy_stop();

}  // namespace helmx
