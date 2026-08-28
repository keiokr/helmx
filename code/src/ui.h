// ui.h — Web dashboard (status / rules / actions)
#pragma once
namespace helmx {

// ui [--port 8090] — serve embedded dashboard
int ui_main(int argc, char** argv);

// Wait for the local proxy, then run one activation self-check.
void ui_auto_activate();

}  // namespace helmx
