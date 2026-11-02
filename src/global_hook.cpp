#include "global_hook.hpp"
#include <fstream>
#include <algorithm>

namespace ak {
namespace {
std::wstring global_hook_mutex_name() {
    wchar_t user[256]{};
    DWORD size = 256;
    std::wstring name = L"Local\\AutoKeyboard.GlobalHook.v1.";
    if (GetUserNameW(user, &size)) name += user;
    wchar_t directory[32768]{};
    DWORD count = GetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR", directory, 32768);
    if (count && count < 32768) {
        uint64_t hash = 14695981039346656037ull;
        for (wchar_t ch : std::wstring_view(directory, count)) {
            hash ^= static_cast<uint16_t>(ch);
            hash *= 1099511628211ull;
        }
        name += L"." + std::to_wstring(hash);
    }
    return name;
}

HKL find_layout_for_language(const std::string& language) {
    WORD target_primary = (language == "ar") ? LANG_ARABIC : LANG_ENGLISH;
    int count = GetKeyboardLayoutList(0, nullptr);
    if (count > 0) {
        std::vector<HKL> list(static_cast<size_t>(count));
        int got = GetKeyboardLayoutList(count, list.data());
        HKL fallback = nullptr;
        for (int i = 0; i < got; ++i) {
            UINT_PTR value = reinterpret_cast<UINT_PTR>(list[i]);
            WORD langid = LOWORD(value);
            if ((langid & 0x3FF) == target_primary) {
                if ((target_primary == LANG_ENGLISH && langid == 0x0409) ||
                    (target_primary == LANG_ARABIC && langid == 0x0401)) {
                    return list[i];
                }
                if (!fallback) fallback = list[i];
            }
        }
        if (fallback) return fallback;
    }
    return LoadKeyboardLayoutW(language == "ar" ? L"00000401" : L"00000409", KLF_NOTELLSHELL);
}
}

bool is_global_hook_active() {
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, global_hook_mutex_name().c_str());
    if (!mutex) return false;
    CloseHandle(mutex);
    return true;
}

std::string detect_foreground_language(HWND foreground) {
    if (!foreground) foreground = GetForegroundWindow();
    if (!foreground) return "en";
    DWORD tid = GetWindowThreadProcessId(foreground, nullptr);
    HKL hkl = GetKeyboardLayout(tid);
    WORD primary = LOWORD(reinterpret_cast<UINT_PTR>(hkl)) & 0x3FF;
    if (primary == LANG_ARABIC) return "ar";
    if (primary == LANG_ENGLISH) return "en";
    return {};
}

std::filesystem::path protected_words_path() {
    return data_folder() / L"protected-words.txt";
}

void ensure_protected_words_file(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (std::filesystem::exists(path, ec)) return;
    std::ofstream out(path, std::ios::binary);
    out << "# Protected Words for AutoKeyboard\r\n"
        << "# Words listed here (one per line) will never be auto-converted.\r\n"
        << "# Lines starting with # are comments.\r\n";
}

GlobalHook* GlobalHook::instance_ = nullptr;

GlobalHook::GlobalHook(Engine& engine, Settings& settings, HookCallbacks callbacks)
    : engine_(engine), settings_(settings), callbacks_(std::move(callbacks)) {
    if (!callbacks_.replace_text) {
        callbacks_.replace_text = [this](size_t backspaces, const std::wstring& replacement, bool release_modifiers) {
            default_replace_text(backspaces, replacement, release_modifiers);
        };
    }
    if (!callbacks_.switch_layout) {
        callbacks_.switch_layout = [this](HWND foreground, HWND focus, const std::string& language) {
            default_switch_layout(foreground, focus, language);
        };
    }
    if (!callbacks_.detect_language) {
        callbacks_.detect_language = [](HWND foreground) {
            return detect_foreground_language(foreground);
        };
    }
    if (!callbacks_.target_allowed) {
        callbacks_.target_allowed = [this](HWND foreground, HWND focus) {
            return default_target_allowed(foreground, focus);
        };
    }
}

GlobalHook::~GlobalHook() {
    stop();
}

bool GlobalHook::start() {
    if (keyboard_hook_) return true;
    instance_ = this;
    HINSTANCE module = GetModuleHandleW(nullptr);
    keyboard_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, module, 0);
    if (!keyboard_hook_) {
        instance_ = nullptr;
        return false;
    }
    mouse_hook_ = SetWindowsHookExW(WH_MOUSE_LL, mouse_proc, module, 0);
    active_mutex_ = CreateMutexW(nullptr, TRUE, global_hook_mutex_name().c_str());
    reset_all();
    return true;
}

void GlobalHook::stop() {
    if (keyboard_hook_) {
        UnhookWindowsHookEx(keyboard_hook_);
        keyboard_hook_ = nullptr;
    }
    if (mouse_hook_) {
        UnhookWindowsHookEx(mouse_hook_);
        mouse_hook_ = nullptr;
    }
    if (active_mutex_) {
        CloseHandle(active_mutex_);
        active_mutex_ = nullptr;
    }
    if (instance_ == this) instance_ = nullptr;
    reset_all();
}

void GlobalHook::clear(bool valid) {
    strokes_.clear();
    valid_word_ = valid;
    manual_word_ = false;
}

void GlobalHook::reset_all() {
    clear(true);
    undo_ = {};
    last_word_ = {};
    lshift_ = rshift_ = shift_ = false;
    lctrl_ = rctrl_ = ctrl_ = false;
    lalt_ = ralt_ = alt_ = false;
    lwin_ = rwin_ = win_ = false;
    alt_shift_armed_ = false;
}

void GlobalHook::default_replace_text(size_t backspaces, const std::wstring& replacement, bool release_modifiers) {
    std::vector<INPUT> inputs;
    inputs.reserve((release_modifiers ? 9 : 0) + (backspaces + replacement.size()) * 2);
    auto add_vk = [&](WORD vk, DWORD flags) {
        INPUT item{};
        item.type = INPUT_KEYBOARD;
        item.ki.wVk = vk;
        item.ki.dwFlags = flags;
        item.ki.dwExtraInfo = SyntheticInputTag;
        inputs.push_back(item);
    };
    auto add_unicode = [&](wchar_t ch, DWORD flags) {
        INPUT item{};
        item.type = INPUT_KEYBOARD;
        item.ki.wVk = 0;
        item.ki.wScan = static_cast<WORD>(ch);
        item.ki.dwFlags = KEYEVENTF_UNICODE | flags;
        item.ki.dwExtraInfo = SyntheticInputTag;
        inputs.push_back(item);
    };
    if (release_modifiers) {
        for (WORD vk : {VK_LCONTROL, VK_RCONTROL, VK_CONTROL, VK_LMENU, VK_RMENU, VK_MENU, VK_LSHIFT, VK_RSHIFT, VK_SHIFT}) {
            add_vk(vk, KEYEVENTF_KEYUP);
        }
    }
    for (size_t i = 0; i < backspaces; ++i) {
        add_vk(VK_BACK, 0);
        add_vk(VK_BACK, KEYEVENTF_KEYUP);
    }
    for (wchar_t ch : replacement) {
        add_unicode(ch, 0);
        add_unicode(ch, KEYEVENTF_KEYUP);
    }
    if (!inputs.empty()) {
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }
}

void GlobalHook::default_switch_layout(HWND foreground, HWND focus, const std::string& language) {
    HKL hkl = find_layout_for_language(language);
    if (!hkl) return;
    if (!foreground) foreground = GetForegroundWindow();
    if (foreground) {
        PostMessageW(foreground, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(hkl));
    }
    if (focus && focus != foreground) {
        PostMessageW(focus, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(hkl));
    }
}

bool GlobalHook::default_target_allowed(HWND foreground, HWND focus) {
    if (!foreground) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    if (!pid) return false;
    // Do not intercept typing inside AutoKeyboard's own physical-key preview edit box (ID 111).
    if (pid == GetCurrentProcessId() && focus && GetDlgCtrlID(focus) == 111) {
        return false;
    }
    if (focus) {
        LONG_PTR style = GetWindowLongPtrW(focus, GWL_STYLE);
        if ((style & ES_PASSWORD) != 0) return false;
    }
    if (pid != cached_pid_) {
        cached_pid_ = pid;
        cached_exe_.clear();
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) {
            wchar_t path[32768]{};
            DWORD size = 32768;
            if (QueryFullProcessImageNameW(process, 0, path, &size) && size > 0) {
                cached_exe_ = std::filesystem::path(std::wstring_view(path, size)).filename().wstring();
            }
            CloseHandle(process);
        }
    }
    if (!cached_exe_.empty() && app_excluded(settings_.excluded_apps, cached_exe_)) {
        return false;
    }
    return true;
}

Decision GlobalHook::decide_with_trailing_punctuation(const std::vector<Stroke>& strokes, const std::string& current_lang) const {
    Decision d = engine_.decide(strokes);
    if (d.changed || strokes.size() <= 1) return d;
    if (d.reason != "unknown" && d.reason != "short-or-malformed") return d;
    size_t max_punct = std::min<size_t>(4, strokes.size() - 1);
    for (size_t punct_count = 1; punct_count <= max_punct; ++punct_count) {
        std::vector<Stroke> core(strokes.begin(), strokes.end() - punct_count);
        Decision core_d = engine_.decide(core);
        if (core_d.changed && core_d.language != current_lang) {
            std::vector<Stroke> punct_strokes(strokes.end() - punct_count, strokes.end());
            std::wstring tail = engine_.render(punct_strokes, core_d.language);
            if (!tail.empty() && letter_count(tail) == 0) {
                core_d.text += tail;
                return core_d;
            }
        }
    }
    return d;
}

void GlobalHook::process_mouse(WPARAM message) {
    if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN ||
        message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        clear(true);
        undo_ = {};
        last_word_ = {};
    }
}

bool GlobalHook::process_key(WPARAM message, const KBDLLHOOKSTRUCT& info, HWND foreground, HWND focus) {
    if ((info.flags & LLKHF_INJECTED) != 0) {
        if (info.dwExtraInfo == SyntheticInputTag) return false;
        clear(false);
        return false;
    }
    bool down = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
    bool up = (message == WM_KEYUP || message == WM_SYSKEYUP);
    if (!down && !up) return false;

    if (!foreground) foreground = GetForegroundWindow();
    if (!focus && foreground) {
        DWORD tid = GetWindowThreadProcessId(foreground, nullptr);
        GUITHREADINFO gui{};
        gui.cbSize = sizeof(gui);
        if (tid && GetGUIThreadInfo(tid, &gui) && gui.hwndFocus) focus = gui.hwndFocus;
        else focus = foreground;
    }

    if (foreground != target_foreground_ || focus != target_focus_) {
        target_foreground_ = foreground;
        target_focus_ = focus;
        clear(true);
        undo_ = {};
        last_word_ = {};
    }

    switch (info.vkCode) {
    case VK_LSHIFT: lshift_ = down; shift_ = lshift_ || rshift_; break;
    case VK_RSHIFT: rshift_ = down; shift_ = lshift_ || rshift_; break;
    case VK_SHIFT: lshift_ = rshift_ = shift_ = down; break;
    case VK_LCONTROL: lctrl_ = down; ctrl_ = lctrl_ || rctrl_; break;
    case VK_RCONTROL: rctrl_ = down; ctrl_ = lctrl_ || rctrl_; break;
    case VK_CONTROL: lctrl_ = rctrl_ = ctrl_ = down; break;
    case VK_LMENU: lalt_ = down; alt_ = lalt_ || ralt_; break;
    case VK_RMENU: ralt_ = down; alt_ = lalt_ || ralt_; break;
    case VK_MENU: lalt_ = ralt_ = alt_ = down; break;
    case VK_LWIN: lwin_ = down; win_ = lwin_ || rwin_; break;
    case VK_RWIN: rwin_ = down; win_ = lwin_ || rwin_; break;
    default: break;
    }

    bool is_modifier = (info.vkCode == VK_SHIFT || info.vkCode == VK_LSHIFT || info.vkCode == VK_RSHIFT ||
                        info.vkCode == VK_CONTROL || info.vkCode == VK_LCONTROL || info.vkCode == VK_RCONTROL ||
                        info.vkCode == VK_MENU || info.vkCode == VK_LMENU || info.vkCode == VK_RMENU ||
                        info.vkCode == VK_LWIN || info.vkCode == VK_RWIN);

    if (is_modifier) {
        if (ctrl_ || win_) {
            alt_shift_armed_ = false;
        } else if (down && alt_ && shift_ && !alt_shift_armed_) {
            alt_shift_armed_ = true;
            alt_shift_initial_lang_ = callbacks_.detect_language(foreground);
        } else if (up && alt_shift_armed_ && (!alt_ || !shift_)) {
            alt_shift_armed_ = false;
            if (settings_.alt_shift_shortcut) {
                std::string current = !alt_shift_initial_lang_.empty() ? alt_shift_initial_lang_ : callbacks_.detect_language(foreground);
                std::string next = (current == "ar") ? "en" : "ar";
                callbacks_.switch_layout(foreground, focus, next);
                clear(true);
                manual_word_ = true;
            }
        }
        return false;
    }

    alt_shift_armed_ = false;
    if (!down) return false;

    if (callbacks_.sync_os_modifiers) {
        lshift_ = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
        rshift_ = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
        shift_ = lshift_ || rshift_;
        lctrl_ = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
        rctrl_ = (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;
        ctrl_ = lctrl_ || rctrl_;
        lalt_ = (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
        ralt_ = (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
        alt_ = lalt_ || ralt_;
        lwin_ = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0;
        rwin_ = (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
        win_ = lwin_ || rwin_;
    }

    if (ctrl_ && alt_ && !win_) {
        if (info.vkCode == 'L' || info.vkCode == 'P') {
            settings_.locked = !settings_.locked;
            engine_.locked = settings_.locked;
            if (callbacks_.lock_changed) callbacks_.lock_changed(settings_.locked);
            clear(true);
            return true;
        }
        if (info.vkCode == VK_BACK) {
            if (undo_.active && strokes_.empty() &&
                static_cast<DWORD>(info.time - undo_.time) <= 10000 &&
                undo_.foreground == foreground && undo_.focus == focus) {
                auto saved = undo_;
                undo_ = {};
                clear(true);
                manual_word_ = true;
                callbacks_.replace_text(saved.replacement_chars, saved.original_text, true);
                callbacks_.switch_layout(foreground, focus, saved.original_lang);
                return true;
            }
            clear(false);
            return false;
        }
        if (info.vkCode == VK_SPACE) {
            if (!callbacks_.target_allowed(foreground, focus)) return false;
            std::string current_lang = callbacks_.detect_language(foreground);
            if (current_lang != "en" && current_lang != "ar") return false;
            if (!strokes_.empty()) {
                auto current_text = engine_.render(strokes_, current_lang);
                auto choice = engine_.manual(strokes_, current_lang);
                if (!current_text.empty() && !choice.text.empty() && choice.changed) {
                    undo_ = {true, current_text, choice.text.size(), current_lang, foreground, focus, info.time};
                    last_word_ = {strokes_, choice.language, choice.text.size(), foreground, focus, info.time, false};
                    clear(true);
                    manual_word_ = true;
                    callbacks_.replace_text(current_text.size(), choice.text, true);
                    callbacks_.switch_layout(foreground, focus, choice.language);
                    return true;
                }
            } else if (!last_word_.strokes.empty() &&
                       static_cast<DWORD>(info.time - last_word_.time) <= 10000 &&
                       last_word_.foreground == foreground && last_word_.focus == focus) {
                auto choice = engine_.manual(last_word_.strokes, last_word_.language);
                if (!choice.text.empty() && choice.changed) {
                    size_t to_delete = last_word_.displayed_chars + (last_word_.trailing_space ? 1 : 0);
                    std::wstring replacement = choice.text + (last_word_.trailing_space ? L" " : L"");
                    std::wstring prev_rendered = engine_.render(last_word_.strokes, last_word_.language) + (last_word_.trailing_space ? L" " : L"");
                    undo_ = {true, prev_rendered, replacement.size(), last_word_.language, foreground, focus, info.time};
                    last_word_.language = choice.language;
                    last_word_.displayed_chars = choice.text.size();
                    last_word_.time = info.time;
                    clear(true);
                    manual_word_ = true;
                    callbacks_.replace_text(to_delete, replacement, true);
                    callbacks_.switch_layout(foreground, focus, choice.language);
                    return true;
                }
            }
            return false;
        }
    }

    if (ctrl_ || alt_ || win_) {
        clear(true);
        undo_ = {};
        last_word_ = {};
        return false;
    }

    if (!callbacks_.target_allowed(foreground, focus)) {
        clear(true);
        return false;
    }

    std::string current_lang = callbacks_.detect_language(foreground);
    if (current_lang != "en" && current_lang != "ar") {
        clear(true);
        return false;
    }
    engine_.current = current_lang;

    if (!strokes_.empty() && last_key_time_ != 0 && static_cast<DWORD>(info.time - last_key_time_) > 5000) {
        clear(true);
    }
    last_key_time_ = info.time;

    switch (info.vkCode) {
    case VK_RETURN:
    case VK_TAB:
    case VK_ESCAPE:
        clear(true);
        undo_ = {};
        last_word_ = {};
        return false;
    case VK_BACK:
        undo_ = {};
        last_word_ = {};
        if (valid_word_ && !strokes_.empty()) {
            strokes_.pop_back();
            return false;
        }
        clear(true);
        return false;
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_INSERT:
    case VK_DELETE:
        undo_ = {};
        last_word_ = {};
        clear(true);
        return false;
    default:
        break;
    }

    if (info.vkCode == VK_SPACE) {
        if (strokes_.empty() || !valid_word_ || manual_word_) {
            clear(true);
            return false;
        }
        auto strokes = std::move(strokes_);
        clear(true);
        std::wstring displayed = engine_.render(strokes, current_lang);
        if (settings_.locked || engine_.locked) {
            last_word_ = {std::move(strokes), current_lang, displayed.size(), foreground, focus, info.time, true};
            return false;
        }
        Decision decision = decide_with_trailing_punctuation(strokes, current_lang);
        if (decision.changed && !displayed.empty() && !decision.text.empty()) {
            std::wstring replacement = decision.text + L" ";
            undo_ = {true, displayed + L" ", replacement.size(), current_lang, foreground, focus, info.time};
            last_word_ = {std::move(strokes), decision.language, decision.text.size(), foreground, focus, info.time, true};
            callbacks_.replace_text(displayed.size(), replacement, shift_);
            callbacks_.switch_layout(foreground, focus, decision.language);
            return true;
        }
        undo_ = {};
        last_word_ = {std::move(strokes), current_lang, displayed.size(), foreground, focus, info.time, true};
        return false;
    }

    undo_ = {};
    last_word_ = {};

    // Digits and hyphen/underscore mid-word indicate identifiers/numbers (e.g., mp4, utf8, user_id).
    if ((!shift_ && info.vkCode >= 0x30 && info.vkCode <= 0x39) ||
        (info.vkCode >= VK_NUMPAD0 && info.vkCode <= VK_NUMPAD9) ||
        info.vkCode == 0xBD) {
        clear(false);
        return false;
    }

    bool caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
    Stroke stroke{static_cast<unsigned>(info.vkCode), shift_, caps};
    if (!engine_.mapped(stroke.key)) {
        clear(false);
        return false;
    }
    if (valid_word_) {
        if (strokes_.size() >= 64) clear(false);
        else strokes_.push_back(stroke);
    }
    return false;
}

LRESULT CALLBACK GlobalHook::keyboard_proc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && instance_ && lParam) {
        const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        try {
            if (instance_->process_key(wParam, *info)) return 1;
        } catch (...) { }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK GlobalHook::mouse_proc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && instance_) {
        try { instance_->process_mouse(wParam); } catch (...) { }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
}
