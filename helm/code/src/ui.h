// ui.h — Web dashboard (status / rules / actions)
#pragma once
namespace helmx {

// ui [--port 8090] — serve embedded dashboard
int ui_main(int argc, char** argv);

// 自动执行一次激活（发送激活词给 codex）。双击启动软件时调用，
// 让会话带上 AGENTS 角色上下文。
void ui_auto_activate();

}  // namespace helmx
