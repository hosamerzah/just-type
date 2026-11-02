#pragma once
#include "runtime.hpp"
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace ak {
inline constexpr ULONG_PTR SyntheticInputTag = 0x41454B48; // "AEKH"

bool is_global_hook_active();
std::string detect_foreground_language(HWND foreground = nullptr);
std::filesystem::path protected_words_path();
void ensure_protected_words_file(const std::filesystem::path& path);

struct HookCallbacks {
    std::function<void(size_t backspaces, const std::wstring& replacement, bool release_modifiers)> replace_text;
    std::function<void(HWND foreground, HWND focus, const std::string& language)> switch_layout;
    std::function<void(bool locked)> lock_changed;
    std::function<std::string(HWND foreground)> detect_language;
    std::function<bool(HWND foreground, HWND focus)> target_allowed;
    bool sync_os_modifiers = false;
};

class GlobalHook {
    struct UndoState {
        bool active = false;
        std::wstring original_text;
        size_t replacement_chars = 0;
        std::string original_lang;
        HWND foreground = nullptr;
        HWND focus = nullptr;
        DWORD time = 0;
    };
    struct LastWordState {
        std::vector<Stroke> strokes;
        std::string language;
        size_t displayed_chars = 0;
        HWND foreground = nullptr;
        HWND focus = nullptr;
        DWORD time = 0;
        bool trailing_space = false;
    };

    Engine& engine_;
    Settings& settings_;
    HookCallbacks callbacks_;
    HHOOK keyboard_hook_ = nullptr;
    HHOOK mouse_hook_ = nullptr;
    HANDLE active_mutex_ = nullptr;

    std::vector<Stroke> strokes_;
    bool valid_word_ = true;
    bool manual_word_ = false;
    DWORD last_key_time_ = 0;
    HWND target_foreground_ = nullptr;
    HWND target_focus_ = nullptr;

    bool lshift_ = false, rshift_ = false, shift_ = false;
    bool lctrl_ = false, rctrl_ = false, ctrl_ = false;
    bool lalt_ = false, ralt_ = false, alt_ = false;
    bool lwin_ = false, rwin_ = false, win_ = false;
    bool alt_shift_armed_ = false;
    std::string alt_shift_initial_lang_;

    UndoState undo_{};
    LastWordState last_word_{};

    DWORD cached_pid_ = 0;
    std::wstring cached_exe_;

    Decision decide_with_trailing_punctuation(const std::vector<Stroke>& strokes, const std::string& current_lang) const;
    void default_replace_text(size_t backspaces, const std::wstring& replacement, bool release_modifiers);
    void default_switch_layout(HWND foreground, HWND focus, const std::string& language);
    bool default_target_allowed(HWND foreground, HWND focus);

    static GlobalHook* instance_;
    static LRESULT CALLBACK keyboard_proc(int code, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK mouse_proc(int code, WPARAM wParam, LPARAM lParam);

public:
    GlobalHook(Engine& engine, Settings& settings, HookCallbacks callbacks = {});
    ~GlobalHook();
    GlobalHook(const GlobalHook&) = delete;
    GlobalHook& operator=(const GlobalHook&) = delete;

    bool start();
    void stop();
    bool active() const noexcept { return keyboard_hook_ != nullptr; }
    void clear(bool valid = true);
    void reset_all();

    bool process_key(WPARAM message, const KBDLLHOOKSTRUCT& info, HWND foreground = nullptr, HWND focus = nullptr);
    void process_mouse(WPARAM message);

    const std::vector<Stroke>& buffered_strokes() const noexcept { return strokes_; }
    bool is_valid_word() const noexcept { return valid_word_; }
    bool is_manual_word() const noexcept { return manual_word_; }
};
}
