#pragma once
#include "win_compat.hpp"
#include <string>
namespace ak {
struct DesktopPreflight { bool ready=false; std::wstring report; };
DesktopPreflight desktop_preflight();
HRESULT configure_current_user(bool install=true);
HRESULT enable_keyboard(bool enabled=true);
HRESULT activate_keyboard(bool session);
std::wstring keyboard_diagnostics();
std::wstring hresult_text(HRESULT result);
}
