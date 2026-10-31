#pragma once
#include "engine.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <atomic>
struct sqlite3;
namespace ak {
struct Settings {
    Mode mode = Mode::Cautious;
    bool locked = false;
    size_t bank_minimum = 4;
    size_t switch_minimum = 4;
    std::wstring excluded_apps = L"WindowsTerminal.exe;powershell.exe;pwsh.exe;cmd.exe";
    bool global_hook = true;
    bool alt_shift_shortcut = true;
};
struct ImportBatch {
    std::vector<LanguageSpec> languages = default_languages();
    std::map<std::string, std::map<std::wstring, int64_t>> words;
    int pages = 0, skipped_pages = 0, rejected_tokens = 0;
    size_t minimum = 4;
    std::wstring source;
    size_t unique_count() const;
};
struct ImportRecord { int64_t id; std::wstring source; int words, skipped; };
struct BankSnapshot { Settings settings; Vocabulary vocabulary; int64_t revision = -1; };
class Store {
    sqlite3* db_ = nullptr;
    void exec(const char* sql);
public:
    explicit Store(const std::filesystem::path& file, bool readonly = false);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Vocabulary vocabulary() const;
    Settings settings() const;
    void save_settings(const Settings& settings);
    void prefer(const std::string& signature, const std::string& language);
    int64_t add_import(const ImportBatch& batch, const std::atomic_bool* cancelled=nullptr);
    bool undo_import(int64_t id);
    std::vector<ImportRecord> imports() const;
    int64_t revision() const;
    BankSnapshot snapshot();
    void cancellation(const std::atomic_bool* cancelled);
};
}
