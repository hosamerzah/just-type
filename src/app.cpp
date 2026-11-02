#include "pdf_import.hpp"
#include "runtime.hpp"
#include "profile.hpp"
#include "tsf.hpp"
#include "global_hook.hpp"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <objbase.h>
#include <thread>
#include <mutex>
#include <memory>
#include <sstream>
#include <algorithm>
#include "build_identity.hpp"

namespace {
using namespace ak;
constexpr UINT TrayCallback = WM_APP + 1, ImportDone = WM_APP + 2;
enum Id { ModeSelect=100, BankFilter, BankMinimum, SwitchMinimum, Lock, Exclusions, Save,
          Import, CancelImport, History, UndoImport, PreviewKeys, PreviewLanguage, PreviewResult, Prefer, OpenFolder, ImportTarget,
          ActivateKeyboard=117, Diagnostics,
          GlobalHookToggle=119, AltShiftToggle=120, ProtectedWords=121,
          TrayShow=200, TrayCautious, TrayAutomatic, TrayLock, TrayExit, TrayActivate, TrayDiagnostics,
          TrayGlobalHook=207, TrayProtectedWords=208 };
std::wstring get_text(HWND window) {
    std::wstring text(GetWindowTextLengthW(window) + 1, 0);
    int n = GetWindowTextW(window, text.data(), static_cast<int>(text.size())); text.resize(n); return text;
}
struct ImportOutcome { ImportBatch batch; int64_t id = 0; std::wstring error; };
class App {
    HWND window_{};
    HFONT font_{}, titleFont_{}, sectionFont_{};
    HICON icon_{};
    NOTIFYICONDATAW tray_{};
    Engine engine_;
    Settings settings_;
    Status shared_;
    std::unique_ptr<Store> store_;
    std::unique_ptr<GlobalHook> hook_;
    std::filesystem::path bank_, protectedFile_;
    std::filesystem::file_time_type protectedMtime_{};
    std::vector<ImportRecord> history_;
    std::thread worker_;
    std::atomic_bool cancelled_{false}, importing_{false};
    std::mutex outcomeMutex_;
    std::unique_ptr<ImportOutcome> outcome_;
    std::wstring notice_ = L"Universal Global Hook is active: works in all apps and switches English / Arabic automatically.";
    std::wstring lastStatus_;
    bool statusLocked_ = false, statusActive_ = true;
    HBRUSH bgBrush_{}, cardBrush_{}, accentBrush_{};
    HPEN borderPen_{};
    HWND heading_{}, caption_{}, status_{}, noticeLabel_{}, previewLabel_{}, shortcutsLabel_{};
    HWND swHeader_{}, wbHeader_{}, tryHeader_{};
    UINT taskbarCreated_=RegisterWindowMessageW(L"TaskbarCreated");
    int dpi_=96,scroll_=0;
    int scaled(int value)const{return MulDiv(value,dpi_,96);}
    HWND control(int id) const { return GetDlgItem(window_, id); }
    HWND add(const wchar_t* cls, const wchar_t* label, DWORD style, int id, int x, int y, int width, int height) {
        if (wcscmp(cls, L"STATIC") == 0) style |= SS_NOPREFIX;
        auto result = CreateWindowExW(wcscmp(cls, L"EDIT") == 0 || wcscmp(cls, L"LISTBOX") == 0 ? WS_EX_CLIENTEDGE : 0,
            cls, label, WS_CHILD | WS_VISIBLE | style, scaled(x), scaled(y), scaled(width), scaled(height), window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        return result;
    }
    void label(const wchar_t* text, int x, int y, int w, int h=22) { add(L"STATIC", text, 0, 0, x,y,w,h); }
    void error(const std::exception& exception) { MessageBoxW(window_, wide(exception.what()).c_str(), L"Just Type (اكتب بلا هم)", MB_OK | MB_ICONERROR); }
    void activate() {
        HRESULT hr=configure_current_user();
        if(hr==S_OK)hr=activate_keyboard(true);
        if(hr!=S_OK) {
            notice_=L"Windows could not activate the TSF keyboard ("+hresult_text(hr)+L"). Note: Universal Global Hook already works without TSF.";
        } else notice_=L"TSF keyboard activation requested. Universal Global Hook is also active across all apps.";
        SetWindowTextW(noticeLabel_,notice_.c_str());
    }
    void diagnostics() { MessageBoxW(window_,keyboard_diagnostics().c_str(),L"Just Type (اكتب بلا هم) - Diagnostics",MB_OK|MB_ICONINFORMATION); }
    static bool hook_allowed_in_process() {
        wchar_t dataDir[32768]{};
        DWORD n = GetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR", dataDir, 32768);
        if (!n || n >= 32768) return true;
        wchar_t force[16]{};
        return GetEnvironmentVariableW(L"AUTOKEYBOARD_ENABLE_HOOK_IN_TESTS", force, 16) > 0;
    }
    void reload_protected_words(bool force = false) {
        std::error_code ec;
        ensure_protected_words_file(protectedFile_);
        auto mtime = std::filesystem::last_write_time(protectedFile_, ec);
        if (!ec && (force || mtime != protectedMtime_)) {
            protectedMtime_ = mtime;
            engine_.load_protected_words(protectedFile_);
        }
    }
    void sync_hook() {
        if (!hook_) return;
        if (settings_.global_hook && hook_allowed_in_process()) hook_->start();
        else hook_->stop();
    }
    void edit_protected_words() {
        ensure_protected_words_file(protectedFile_);
        ShellExecuteW(window_, L"open", L"notepad.exe", (L"\"" + protectedFile_.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        notice_ = L"Editing protected-words.txt. Save the file in Notepad and Just Type will reload it automatically.";
        SetWindowTextW(noticeLabel_, notice_.c_str());
    }
    size_t minimum(int id) {
        auto text = get_text(control(id));
        size_t used = 0, n = 0;
        try { n = std::stoul(text, &used); } catch (...) { }
        if (used != text.size() || n < 1 || n > 32) throw std::runtime_error("Minimum word lengths must be between 1 and 32");
        return n;
    }
    Settings selected_settings() {
        Settings result = settings_;
        result.mode = SendMessageW(control(ModeSelect), CB_GETCURSEL, 0, 0) == 1 ? Mode::Automatic : Mode::Cautious;
        result.locked = SendMessageW(control(Lock), BM_GETCHECK, 0, 0) == BST_CHECKED;
        result.bank_minimum = SendMessageW(control(BankFilter), BM_GETCHECK, 0, 0) == BST_CHECKED ? minimum(BankMinimum) : 1;
        result.switch_minimum = minimum(SwitchMinimum);
        result.excluded_apps = get_text(control(Exclusions));
        if (control(GlobalHookToggle)) result.global_hook = SendMessageW(control(GlobalHookToggle), BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (control(AltShiftToggle)) result.alt_shift_shortcut = SendMessageW(control(AltShiftToggle), BM_GETCHECK, 0, 0) == BST_CHECKED;
        return result;
    }
    void save() {
        auto selected = selected_settings();
        store_->save_settings(selected); settings_ = selected; apply(engine_, settings_);
        reload_protected_words(true); sync_hook(); update_tray();
        notice_ = L"Settings saved. Active keyboards refresh when you finish the current word (within 1.5 seconds).";
        SetWindowTextW(noticeLabel_, notice_.c_str());
        preview();
    }
    void auto_save_controls() {
        try {
            settings_ = selected_settings();
        } catch (...) {
            settings_.mode = SendMessageW(control(ModeSelect), CB_GETCURSEL, 0, 0) == 1 ? Mode::Automatic : Mode::Cautious;
            settings_.locked = SendMessageW(control(Lock), BM_GETCHECK, 0, 0) == BST_CHECKED;
            settings_.excluded_apps = get_text(control(Exclusions));
            if (control(GlobalHookToggle)) settings_.global_hook = SendMessageW(control(GlobalHookToggle), BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (control(AltShiftToggle)) settings_.alt_shift_shortcut = SendMessageW(control(AltShiftToggle), BM_GETCHECK, 0, 0) == BST_CHECKED;
        }
        store_->save_settings(settings_); apply(engine_, settings_);
        sync_hook(); update_tray(); preview();
    }
    void settings_to_controls() {
        SendMessageW(control(ModeSelect), CB_SETCURSEL, settings_.mode == Mode::Automatic ? 1 : 0, 0);
        SendMessageW(control(BankFilter), BM_SETCHECK, settings_.bank_minimum > 1 ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(control(BankMinimum), std::to_wstring(settings_.bank_minimum > 1 ? settings_.bank_minimum : 4).c_str());
        EnableWindow(control(BankMinimum), settings_.bank_minimum > 1);
        SetWindowTextW(control(SwitchMinimum), std::to_wstring(settings_.switch_minimum).c_str());
        SendMessageW(control(Lock), BM_SETCHECK, settings_.locked ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(control(Exclusions), settings_.excluded_apps.c_str());
        if (control(GlobalHookToggle)) SendMessageW(control(GlobalHookToggle), BM_SETCHECK, settings_.global_hook ? BST_CHECKED : BST_UNCHECKED, 0);
        if (control(AltShiftToggle)) SendMessageW(control(AltShiftToggle), BM_SETCHECK, settings_.alt_shift_shortcut ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    std::vector<Stroke> preview_strokes() {
        auto text = get_text(control(PreviewKeys));
        std::vector<Stroke> strokes;
        for (wchar_t ch : text) {
            bool found = false;
            for (const auto& pack : engine_.packs) if (pack.id == "en") {
                for (const auto& [key, mapping] : pack.keys) {
                    if (mapping.normal == std::wstring(1, ch) || mapping.shifted == std::wstring(1, ch)) {
                        strokes.push_back({key, mapping.shifted == std::wstring(1,ch), false}); found = true; break;
                    }
                }
            }
            if (!found) return {};
        }
        return strokes;
    }
    void preview() {
        if (!control(PreviewKeys) || !previewLabel_) return;
        try {
            auto selected = selected_settings();
            std::string savedCurrent = engine_.current;
            apply(engine_, selected);
            auto index = SendMessageW(control(PreviewLanguage), CB_GETCURSEL, 0, 0);
            if (index >= 0 && static_cast<size_t>(index) < engine_.packs.size()) engine_.current = engine_.packs[index].id;
            auto strokes = preview_strokes();
            if (strokes.empty()) {
                SetWindowTextW(previewLabel_, L"Enter one word using its US physical keys, for example hgsghl or hello.");
                apply(engine_, settings_);
                engine_.current = savedCurrent;
                return;
            }
            auto choice = engine_.decide(strokes);
            std::wstring text;
            for (const auto& pack : engine_.packs) text += pack.name + L": " + pack.render(strokes) + L"     ";
            text += L"\r\nResult: " + choice.text + L"   (" + wide(choice.reason) + L")";
            SetWindowTextW(previewLabel_, text.c_str());
            apply(engine_, settings_);
            engine_.current = savedCurrent;
        } catch (...) {
            apply(engine_, settings_);
            SetWindowTextW(previewLabel_, L"Set minimum lengths between 1 and 32.");
        }
    }
    void refresh_history() {
        history_ = store_->imports();
        SendMessageW(control(History), LB_RESETCONTENT, 0, 0);
        for (const auto& record : history_) {
            auto line = record.source + L"  |  " + std::to_wstring(record.words) + L" words  |  " + std::to_wstring(record.skipped) + L" pages without text";
            SendMessageW(control(History), LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
        }
        engine_.vocabulary = store_->vocabulary();
        EnableWindow(control(UndoImport), !history_.empty() && !importing_);
        preview();
    }
    void import_pdf() {
        if (importing_) return;
        save();
        wchar_t filename[32768]{};
        OPENFILENAMEW picker{}; picker.lStructSize=sizeof(picker);
        picker.hwndOwner = window_; picker.lpstrFile = filename; picker.nMaxFile = 32768;
        picker.lpstrFilter = L"PDF documents\0*.pdf\0\0";
        picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
        if (!GetOpenFileNameW(&picker)) return;
        if (worker_.joinable()) worker_.join();
        importing_ = true; cancelled_ = false;
        EnableWindow(control(Import), FALSE); EnableWindow(control(CancelImport), TRUE); EnableWindow(control(UndoImport), FALSE);
        SetWindowTextW(noticeLabel_, L"Reading PDF text locally. You can continue typing in other applications.");
        std::filesystem::path path(filename);
        auto min = settings_.bank_minimum;
        std::vector<LanguageSpec> languages;
        auto target=SendMessageW(control(ImportTarget),CB_GETCURSEL,0,0);
        for(size_t i=0;i<engine_.packs.size();++i)if(target<=0 || static_cast<size_t>(target-1)==i)languages.push_back(static_cast<const LanguageSpec&>(engine_.packs[i]));
        worker_ = std::thread([this, path, min, languages=std::move(languages)] {
            auto outcome = std::make_unique<ImportOutcome>();
            try {
                outcome->batch = extract_pdf_isolated(path, min, &cancelled_, languages);
                if (cancelled_) throw std::runtime_error("Import cancelled");
                if (outcome->batch.unique_count() > 0) {
                    Store db(bank_);
                    outcome->id = db.add_import(outcome->batch,&cancelled_);
                }
            } catch (const std::exception& e) { outcome->error = wide(e.what()); }
            { std::lock_guard guard(outcomeMutex_); outcome_ = std::move(outcome); }
            PostMessageW(window_, ImportDone, 0, 0);
        });
    }
    void import_done() {
        if (worker_.joinable()) worker_.join();
        importing_ = false;
        EnableWindow(control(Import), TRUE); EnableWindow(control(CancelImport), FALSE);
        std::unique_ptr<ImportOutcome> outcome;
        { std::lock_guard guard(outcomeMutex_); outcome = std::move(outcome_); }
        if (outcome) {
            if (!outcome->error.empty()) notice_ = outcome->error;
            else notice_ = L"Import complete: " + std::to_wstring(outcome->batch.unique_count()) + L" unique words; " +
                std::to_wstring(outcome->batch.skipped_pages) + L" pages without extractable text; " +
                std::to_wstring(outcome->batch.rejected_tokens) + L" tokens filtered. No OCR was used.";
            SetWindowTextW(noticeLabel_, notice_.c_str());
        }
        refresh_history();
    }
    void update_tray() {
        reload_protected_words(false);
        auto state = shared_.read();
        DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
        if(state.pid!=static_cast<LONG>(foregroundPid) || static_cast<DWORD>(GetTickCount()-state.tick)>4000)state.pid=0;
        if(state.pid) {
            HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(state.pid));
            if(!process || WaitForSingleObject(process,0)!=WAIT_TIMEOUT)state.pid=0;
            if(process)CloseHandle(process);
        }
        bool hookActive = hook_ && hook_->active();
        std::wstring language = L"not active";
        if (hookActive) {
            auto fg = detect_foreground_language();
            language = !fg.empty() ? wide(fg) : L"en";
        } else if (state.pid && state.language >= 0 && static_cast<size_t>(state.language) < engine_.packs.size()) {
            language = wide(engine_.packs[state.language].id);
        }
        std::wstring mode = (!hookActive && state.pid ? state.mode != 0 : settings_.mode == Mode::Automatic) ? L"Automatic" : L"Cautious";
        bool locked = (!hookActive && state.pid ? state.locked != 0 : settings_.locked);
        std::wstring tipText = L"Just Type (اكتب بلا هم) | " + language + L" | " + mode;
        if (hookActive) tipText += L" | Hook";
        if (locked) tipText += L" | Locked";
        wcsncpy_s(tray_.szTip, tipText.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &tray_);

        std::wstring langDisplay = (language == L"ar") ? L"العربية (AR)" : (language == L"en" ? L"English (EN)" : language);
        std::wstring uiStatus;
        if (locked) {
            uiStatus = L"● Locked (" + langDisplay + L")  •  Mode: " + mode + L"  (Ctrl+Alt+L to unlock)";
        } else if (hookActive) {
            uiStatus = L"● Active in all apps  •  Layout: " + langDisplay + L"  •  Mode: " + mode;
        } else if (state.pid) {
            uiStatus = L"● Active (TSF)  •  Layout: " + langDisplay + L"  •  Mode: " + mode;
        } else {
            uiStatus = L"○ Standby  •  Enable Global Hook below or click Activate TSF keyboard";
        }
        statusLocked_ = locked;
        statusActive_ = hookActive || state.pid != 0;
        if (status_ && uiStatus != lastStatus_) {
            lastStatus_ = uiStatus;
            SetWindowTextW(status_, uiStatus.c_str());
            InvalidateRect(status_, nullptr, TRUE);
        }
    }
    void menu() {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, TrayShow, L"Settings and word bank");
        AppendMenuW(menu, MF_STRING | (settings_.global_hook ? MF_CHECKED : 0), TrayGlobalHook, L"Universal global hook & layout switcher");
        AppendMenuW(menu, MF_STRING, TrayProtectedWords, L"Edit protected words...");
        AppendMenuW(menu, MF_STRING, TrayActivate, L"Activate automatic keyboard (TSF)");
        AppendMenuW(menu, MF_STRING, TrayDiagnostics, L"Windows activation diagnostics");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (settings_.mode == Mode::Cautious ? MF_CHECKED : 0), TrayCautious, L"Cautious");
        AppendMenuW(menu, MF_STRING | (settings_.mode == Mode::Automatic ? MF_CHECKED : 0), TrayAutomatic, L"Automatic");
        AppendMenuW(menu, MF_STRING | (settings_.locked ? MF_CHECKED : 0), TrayLock, L"Lock current language");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, TrayExit, L"Exit tray application");
        POINT point{}; GetCursorPos(&point); SetForegroundWindow(window_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr);
        PostMessageW(window_, WM_NULL, 0, 0); DestroyMenu(menu);
    }
    void create_controls() {
        font_ = CreateFontW(-scaled(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        titleFont_ = CreateFontW(-scaled(24),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        sectionFont_ = CreateFontW(-scaled(14),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");

        // Card 1: Header & Live Status (y = 10..122)
        heading_ = add(L"STATIC", L"Just Type — اكتب بلا هم",0,0,74,20,695,34);
        SendMessageW(heading_, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);
        caption_ = add(L"STATIC", L"Smart Arabic & English Keyboard Switcher  •  تبديل تلقائي ذكي بين العربية والإنجليزية",0,0,76,54,695,22);
        status_ = add(L"STATIC", L"● Active in all apps  •  Layout: English (EN)  •  Mode: Automatic",0,0,30,86,445,24);
        SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont_), TRUE);
        add(L"BUTTON",L"Activate TSF keyboard",BS_PUSHBUTTON|WS_TABSTOP,ActivateKeyboard,485,82,165,29);
        add(L"BUTTON",L"Diagnostics",BS_PUSHBUTTON|WS_TABSTOP,Diagnostics,660,82,115,29);

        // Card 2: Switching & Behavior (y = 130..358)
        swHeader_ = add(L"STATIC", L"SWITCHING & BEHAVIOR", 0, 0, 30, 140, 400, 20);
        SendMessageW(swHeader_, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont_), TRUE);
        label(L"Switching mode",30,170,145);
        add(L"COMBOBOX",L"",CBS_DROPDOWNLIST | WS_TABSTOP,ModeSelect,185,166,330,200);
        SendMessageW(control(ModeSelect), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Cautious - only a clear alternative"));
        SendMessageW(control(ModeSelect), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Automatic - rank valid matches"));
        add(L"BUTTON",L"Lock current language",BS_AUTOCHECKBOX | WS_TABSTOP,Lock,535,167,235,25);
        add(L"BUTTON",L"Filter bank by length",BS_AUTOCHECKBOX | WS_TABSTOP,BankFilter,30,200,185,25);
        add(L"EDIT",L"4",ES_NUMBER | WS_TABSTOP,BankMinimum,220,199,55,25);
        label(L"letters minimum (uncheck to include short words)",285,202,485);
        label(L"Automatic switching minimum",30,234,235);
        add(L"EDIT",L"4",ES_NUMBER | WS_TABSTOP,SwitchMinimum,270,231,55,25);
        label(L"letters; manual conversion ignores this limit",335,234,435);
        label(L"Excluded applications (executable names separated by ;)",30,263,590);
        add(L"EDIT",L"",ES_AUTOHSCROLL | WS_TABSTOP,Exclusions,30,286,595,26);
        add(L"BUTTON",L"Save settings",BS_PUSHBUTTON | WS_TABSTOP,Save,640,284,135,29);
        add(L"BUTTON",L"Universal global hook && auto layout switch",BS_AUTOCHECKBOX | WS_TABSTOP,GlobalHookToggle,30,322,355,25);
        add(L"BUTTON",L"Direct Alt+Shift EN/AR switch",BS_AUTOCHECKBOX | WS_TABSTOP,AltShiftToggle,395,322,235,25);
        add(L"BUTTON",L"Protected words...",BS_PUSHBUTTON | WS_TABSTOP,ProtectedWords,640,320,135,28);

        // Card 3: Word Bank & PDF Import (y = 366..558)
        wbHeader_ = add(L"STATIC", L"WORD BANK & PDF IMPORT", 0, 0, 30, 378, 330, 20);
        SendMessageW(wbHeader_, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont_), TRUE);
        label(L"PDF language:",390,378,105);
        add(L"COMBOBOX",L"",CBS_DROPDOWNLIST | WS_TABSTOP,ImportTarget,500,374,275,180);
        SendMessageW(control(ImportTarget),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Automatic by character ranges"));
        for(const auto& pack:engine_.packs)SendMessageW(control(ImportTarget),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(pack.name.c_str()));
        SendMessageW(control(ImportTarget),CB_SETCURSEL,0,0);
        add(L"BUTTON",L"Import PDF...",BS_PUSHBUTTON | WS_TABSTOP,Import,30,408,145,30);
        add(L"BUTTON",L"Cancel import",BS_PUSHBUTTON | WS_TABSTOP,CancelImport,188,408,140,30);
        add(L"BUTTON",L"Undo selected import",BS_PUSHBUTTON | WS_TABSTOP,UndoImport,342,408,195,30);
        add(L"BUTTON",L"Open local bank folder",BS_PUSHBUTTON | WS_TABSTOP,OpenFolder,550,408,225,30);
        EnableWindow(control(CancelImport), FALSE);
        add(L"LISTBOX",L"",LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP,History,30,446,745,68);
        noticeLabel_ = add(L"STATIC",notice_.c_str(),0,0,30,520,745,32);

        // Card 4: Live Preview & Shortcuts (y = 566..716)
        tryHeader_ = add(L"STATIC", L"TRY A WORD — LIVE PREVIEW (physical US keys)", 0, 0, 30, 576, 480, 20);
        SendMessageW(tryHeader_, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont_), TRUE);
        add(L"EDIT",L"hgsghl",ES_AUTOHSCROLL | WS_TABSTOP,PreviewKeys,30,602,275,27);
        add(L"COMBOBOX",L"",CBS_DROPDOWNLIST | WS_TABSTOP,PreviewLanguage,320,602,220,160);
        for (const auto& pack : engine_.packs) SendMessageW(control(PreviewLanguage), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pack.name.c_str()));
        SendMessageW(control(PreviewLanguage), CB_SETCURSEL, 0, 0);
        add(L"BUTTON",L"Prefer this language",BS_PUSHBUTTON | WS_TABSTOP,Prefer,555,600,220,30);
        previewLabel_ = add(L"STATIC",L"",0,PreviewResult,30,636,745,42);
        shortcutsLabel_ = add(L"STATIC",L"Shortcuts:   Ctrl+Alt+Space: Convert     Ctrl+Alt+Backspace: Undo     Ctrl+Alt+L / P: Lock     Alt+Shift: EN / AR",0,0,30,686,745,22);

        settings_to_controls(); refresh_history(); preview();
    }
    LRESULT dispatch(UINT message, WPARAM w, LPARAM l) {
        if(taskbarCreated_ && message==taskbarCreated_){Shell_NotifyIconW(NIM_ADD,&tray_);update_tray();return 0;}
        switch (message) {
        case WM_CREATE: create_controls(); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(window_, &ps);
            RECT rc{}; GetClientRect(window_, &rc);
            FillRect(hdc, &rc, bgBrush_ ? bgBrush_ : reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
            if (cardBrush_ && borderPen_) {
                HGDIOBJ oldBrush = SelectObject(hdc, cardBrush_);
                HGDIOBJ oldPen = SelectObject(hdc, borderPen_);
                int r = scaled(10);
                int left = scaled(16), right = std::max(scaled(790), static_cast<int>(rc.right) - scaled(16));
                RoundRect(hdc, left, scaled(10) - scroll_, right, scaled(122) - scroll_, r, r);
                RoundRect(hdc, left, scaled(130) - scroll_, right, scaled(358) - scroll_, r, r);
                RoundRect(hdc, left, scaled(366) - scroll_, right, scaled(558) - scroll_, r, r);
                RoundRect(hdc, left, scaled(566) - scroll_, right, scaled(716) - scroll_, r, r);
                if (accentBrush_) {
                    RECT stripe{left + scaled(4), scaled(10) - scroll_, right - scaled(4), scaled(14) - scroll_};
                    FillRect(hdc, &stripe, accentBrush_);
                    HGDIOBJ badgeBrush = SelectObject(hdc, accentBrush_);
                    HGDIOBJ badgePen = SelectObject(hdc, GetStockObject(NULL_PEN));
                    RECT badgeRc{scaled(28), scaled(22) - scroll_, scaled(66), scaled(60) - scroll_};
                    RoundRect(hdc, badgeRc.left, badgeRc.top, badgeRc.right, badgeRc.bottom, scaled(10), scaled(10));
                    SelectObject(hdc, badgeBrush);
                    SelectObject(hdc, badgePen);
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(255, 255, 255));
                    HGDIOBJ oldFont = SelectObject(hdc, sectionFont_ ? sectionFont_ : font_);
                    DrawTextW(hdc, L"ع/A", -1, &badgeRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, oldFont);
                }
                SelectObject(hdc, oldBrush);
                SelectObject(hdc, oldPen);
            }
            EndPaint(window_, &ps);
            return 0;
        }
        case WM_TIMER: update_tray(); return 0;
        case WM_SIZE: {
            RECT client{};GetClientRect(window_,&client);
            int contentHeight = scaled(724);
            if (client.bottom >= contentHeight && scroll_ != 0) {
                ScrollWindowEx(window_, 0, scroll_, nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN);
                scroll_ = 0;
            }
            SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,contentHeight,static_cast<UINT>(client.bottom),scroll_,0};
            SetScrollInfo(window_,SB_VERT,&info,TRUE);
            ShowScrollBar(window_, SB_VERT, client.bottom < contentHeight);
            InvalidateRect(window_, nullptr, TRUE);
            return 0;
        }
        case WM_MOUSEWHEEL: SendMessageW(window_,WM_VSCROLL,GET_WHEEL_DELTA_WPARAM(w)>0?SB_LINEUP:SB_LINEDOWN,0);return 0;
        case WM_VSCROLL: {
            SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(window_,SB_VERT,&info);
            int next=scroll_;switch(LOWORD(w)){
                case SB_LINEUP:next-=scaled(32);break;case SB_LINEDOWN:next+=scaled(32);break;
                case SB_PAGEUP:next-=info.nPage;break;case SB_PAGEDOWN:next+=info.nPage;break;
                case SB_THUMBTRACK:next=info.nTrackPos;break;default:return 0;}
            next=std::clamp(next,0,std::max(0,info.nMax-static_cast<int>(info.nPage)+1));
            int dy = scroll_ - next;
            scroll_ = next; SetScrollPos(window_, SB_VERT, scroll_, TRUE);
            ScrollWindowEx(window_, 0, dy, nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN);
            InvalidateRect(window_, nullptr, TRUE); return 0;
        }
        case TrayCallback:
            if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU) menu();
            else if (l == WM_LBUTTONDBLCLK || l == WM_LBUTTONUP) { ShowWindow(window_,SW_SHOW); SetForegroundWindow(window_); }
            return 0;
        case TrayLockMessage:
            settings_.locked = w != 0; apply(engine_, settings_); store_->save_settings(settings_); settings_to_controls(); update_tray(); return 0;
        case ImportDone: import_done(); return 0;
        case WM_COMMAND: {
            int id = LOWORD(w), notification = HIWORD(w);
            if (id == Save) save();
            else if(id==ActivateKeyboard || id==TrayActivate)activate();
            else if(id==Diagnostics || id==TrayDiagnostics)diagnostics();
            else if(id==ProtectedWords || id==TrayProtectedWords)edit_protected_words();
            else if (id == BankFilter) { EnableWindow(control(BankMinimum), SendMessageW(control(BankFilter),BM_GETCHECK,0,0) == BST_CHECKED); auto_save_controls(); }
            else if (id == Import) import_pdf();
            else if (id == CancelImport) { cancelled_ = true; SetWindowTextW(noticeLabel_,L"Cancelling import..."); }
            else if (id == UndoImport) {
                auto selected = SendMessageW(control(History), LB_GETCURSEL, 0, 0);
                if (selected >= 0 && static_cast<size_t>(selected) < history_.size() && !importing_) {
                    store_->undo_import(history_[selected].id); refresh_history();
                    SetWindowTextW(noticeLabel_,L"Import removed. Words shared with other imports are retained.");
                }
            } else if (id == OpenFolder) ShellExecuteW(window_, L"open", bank_.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else if (id == Prefer) {
                auto strokes = preview_strokes(); auto index = SendMessageW(control(PreviewLanguage),CB_GETCURSEL,0,0);
                if (!strokes.empty() && index >= 0 && static_cast<size_t>(index) < engine_.packs.size()) {
                    store_->prefer(Engine::signature(strokes),engine_.packs[index].id);
                    engine_.vocabulary = store_->vocabulary(); preview();
                    SetWindowTextW(noticeLabel_,L"Preference saved for Automatic mode. It only ranks candidates that match the bank.");
                }
            } else if (id == TrayShow) { ShowWindow(window_,SW_SHOW); SetForegroundWindow(window_); }
            else if (id == TrayExit) DestroyWindow(window_);
            else if (id == TrayGlobalHook) {
                settings_.global_hook = !settings_.global_hook;
                store_->save_settings(settings_); sync_hook(); settings_to_controls(); update_tray();
            } else if (id == TrayCautious || id == TrayAutomatic || id == TrayLock) {
                if (id == TrayLock) settings_.locked = !settings_.locked;
                else settings_.mode = id == TrayAutomatic ? Mode::Automatic : Mode::Cautious;
                store_->save_settings(settings_); apply(engine_, settings_); settings_to_controls(); update_tray(); preview();
            } else if (id == Lock || id == GlobalHookToggle || id == AltShiftToggle) {
                auto_save_controls();
            } else if (id == ModeSelect && notification == CBN_SELCHANGE) {
                auto_save_controls();
            } else if (notification == EN_CHANGE || notification == CBN_SELCHANGE) preview();
            return 0;
        }
        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(w);
            RECT rc{}; GetClientRect(window_, &rc);
            FillRect(hdc, &rc, bgBrush_ ? bgBrush_ : reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
            return 1;
        }
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(w);
            HWND ctrl = reinterpret_cast<HWND>(l);
            SetBkMode(hdc, TRANSPARENT);
            if (ctrl == heading_) {
                SetTextColor(hdc, RGB(15, 23, 42));
            } else if (ctrl == swHeader_ || ctrl == wbHeader_ || ctrl == tryHeader_) {
                SetTextColor(hdc, RGB(30, 64, 175));
            } else if (ctrl == status_) {
                if (statusLocked_) SetTextColor(hdc, RGB(180, 83, 9));
                else if (statusActive_) SetTextColor(hdc, RGB(21, 128, 61));
                else SetTextColor(hdc, RGB(100, 116, 139));
            } else if (ctrl == caption_ || ctrl == shortcutsLabel_ || ctrl == noticeLabel_) {
                SetTextColor(hdc, RGB(71, 85, 105));
            } else {
                SetTextColor(hdc, RGB(31, 41, 55));
            }
            return reinterpret_cast<LRESULT>(cardBrush_ ? cardBrush_ : bgBrush_);
        }
        case WM_CTLCOLORDLG: {
            return reinterpret_cast<LRESULT>(bgBrush_);
        }
        case WM_CLOSE: ShowWindow(window_,SW_HIDE); return 0;
        case WM_DESTROY:
            cancelled_ = true;
            if (hook_) hook_->stop();
            if (worker_.joinable()) worker_.join();
            Shell_NotifyIconW(NIM_DELETE,&tray_); KillTimer(window_,1); PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(window_,message,w,l);
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM w,LPARAM l) {
        auto self = reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); self->window_ = window;
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            try { return self->dispatch(message,w,l); }
            catch (const std::exception& e) { self->error(e); return 0; }
        }
        return DefWindowProcW(window,message,w,l);
    }
public:
    ~App() {
        if (hook_) hook_->stop();
        if (worker_.joinable()) { cancelled_ = true; worker_.join(); }
        if (font_) DeleteObject(font_);
        if (titleFont_) DeleteObject(titleFont_);
        if (sectionFont_) DeleteObject(sectionFont_);
        if (bgBrush_) DeleteObject(bgBrush_);
        if (cardBrush_) DeleteObject(cardBrush_);
        if (accentBrush_) DeleteObject(accentBrush_);
        if (borderPen_) DeleteObject(borderPen_);
    }
    int run(bool quiet) {
        dpi_=GetDpiForSystem();
        auto folder = data_folder(); std::filesystem::create_directories(folder); bank_ = folder / L"words.db";
        protectedFile_ = protected_words_path();
        store_ = std::make_unique<Store>(bank_); settings_ = store_->settings();
        engine_.load(packs_folder()); reload_protected_words(true); apply(engine_,settings_);
        bgBrush_ = CreateSolidBrush(RGB(241, 245, 249));
        cardBrush_ = CreateSolidBrush(RGB(255, 255, 255));
        accentBrush_ = CreateSolidBrush(RGB(37, 99, 235));
        borderPen_ = CreatePen(PS_SOLID, 1, RGB(219, 224, 233));
        HookCallbacks callbacks{};
        callbacks.sync_os_modifiers = true;
        callbacks.lock_changed = [this](bool locked) {
            if (window_) PostMessageW(window_, TrayLockMessage, locked ? 1 : 0, 0);
        };
        hook_ = std::make_unique<GlobalHook>(engine_, settings_, std::move(callbacks));
        sync_hook();
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
        icon_ = LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1));
        WNDCLASSW cls{}; cls.lpfnWndProc = proc; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = tray_class().c_str();
        cls.hIcon = icon_; cls.hCursor = LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        RegisterClassW(&cls);
        RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        if (!CreateWindowW(tray_class().c_str(),L"Just Type (اكتب بلا هم) 0.2.0 - Settings and word bank",WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL,
            CW_USEDEFAULT,CW_USEDEFAULT,scaled(835),std::min(scaled(775),static_cast<int>(work.bottom-work.top)),nullptr,nullptr,cls.hInstance,this)) throw std::runtime_error("Cannot create settings window");
        tray_.cbSize = sizeof(tray_); tray_.hWnd = window_; tray_.uID = 1;
        tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; tray_.uCallbackMessage = TrayCallback; tray_.hIcon = icon_;
        wcscpy_s(tray_.szTip,L"Just Type (اكتب بلا هم)"); Shell_NotifyIconW(NIM_ADD,&tray_); SetTimer(window_,1,750,nullptr);
        update_tray();
        if (!quiet) ShowWindow(window_,SW_SHOW);
        MSG message{};
        while (GetMessageW(&message,nullptr,0,0) > 0) if (!IsDialogMessageW(window_,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        return static_cast<int>(message.wParam);
    }
};
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR command,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    if (HWND existing = FindWindowW(ak::tray_class().c_str(),nullptr)) { ShowWindow(existing,SW_SHOW); SetForegroundWindow(existing); return 0; }
    HRESULT com = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result = 1;
    try { App app; result = app.run(command && wcsstr(command,L"--tray")); }
    catch (const std::exception& e) { MessageBoxW(nullptr,ak::wide(e.what()).c_str(),L"Just Type (اكتب بلا هم)",MB_OK | MB_ICONERROR); }
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
