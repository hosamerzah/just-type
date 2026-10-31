#include "store.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <stdexcept>

namespace ak {
namespace {
struct Statement {
    sqlite3_stmt* value = nullptr;
    sqlite3* db;
    Statement(sqlite3* connection, const char* sql) : db(connection) {
        if (sqlite3_prepare_v2(db, sql, -1, &value, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(value); }
    void text(int index, const std::string& s) { if (sqlite3_bind_text(value, index, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db)); }
    void number(int index, int64_t n) { sqlite3_bind_int64(value, index, n); }
    bool row() {
        int result = sqlite3_step(value);
        if (result != SQLITE_ROW && result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
        return result == SQLITE_ROW;
    }
    std::string str(int column) const { auto data = sqlite3_column_text(value, column); return data ? reinterpret_cast<const char*>(data) : ""; }
    int64_t integer(int column) const { return sqlite3_column_int64(value, column); }
    void reset(){sqlite3_reset(value);sqlite3_clear_bindings(value);}
};
}
size_t ImportBatch::unique_count() const { size_t n = 0; for (const auto& [_, words_] : words) n += words_.size(); return n; }
void Store::exec(const char* sql) {
    char* message = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        std::string error = message ? message : "Database error";
        sqlite3_free(message);
        throw std::runtime_error(error);
    }
}
Store::Store(const std::filesystem::path& file, bool readonly) {
    int flags = (readonly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(utf8(file.wstring()).c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        std::string error = db_ ? sqlite3_errmsg(db_) : "Cannot open word bank";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(error);
    }
    sqlite3_busy_timeout(db_, readonly ? 0 : 1000);
    try {
        if (!readonly) {
            exec("PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL;"
                 "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
                 "CREATE TABLE IF NOT EXISTS preferences(signature TEXT PRIMARY KEY,language TEXT NOT NULL);"
                 "CREATE TABLE IF NOT EXISTS imports(id INTEGER PRIMARY KEY,source TEXT NOT NULL,pages INTEGER NOT NULL,skipped INTEGER NOT NULL,minimum INTEGER NOT NULL);"
                 "CREATE TABLE IF NOT EXISTS import_words(import_id INTEGER NOT NULL REFERENCES imports(id) ON DELETE CASCADE,language TEXT NOT NULL,word TEXT NOT NULL,count INTEGER NOT NULL CHECK(count>0),PRIMARY KEY(import_id,language,word));"
                 "CREATE TABLE IF NOT EXISTS meta(id INTEGER PRIMARY KEY CHECK(id=1),revision INTEGER NOT NULL);"
                 "INSERT OR IGNORE INTO meta VALUES(1,0);");
        }
    } catch (...) { sqlite3_close(db_); db_ = nullptr; throw; }
}
Store::~Store() { if (db_) sqlite3_close(db_); }
Vocabulary Store::vocabulary() const {
    Vocabulary result;
    Statement words(db_, "SELECT language,word,SUM(count) FROM import_words GROUP BY language,word");
    while (words.row()) {
        auto word = wide(words.str(1));
        if (letter_count(word)>0) result.counts[words.str(0)][word] = words.integer(2);
    }
    Statement prefs(db_, "SELECT signature,language FROM preferences");
    while (prefs.row()) result.preferences[prefs.str(0)] = prefs.str(1);
    return result;
}
Settings Store::settings() const {
    Settings result;
    Statement query(db_, "SELECT key,value FROM settings");
    while (query.row()) {
        auto key = query.str(0), value = query.str(1);
        if (key == "mode") result.mode = value == "automatic" ? Mode::Automatic : Mode::Cautious;
        else if (key == "locked") result.locked = value == "1";
        else if (key == "bank_minimum" || key == "switch_minimum") {
            size_t n = 4;
            try { n = std::clamp<size_t>(std::stoul(value), 1, 32); } catch (...) { }
            if (key == "bank_minimum") result.bank_minimum = n;
            else result.switch_minimum = n;
        } else if (key == "excluded_apps") result.excluded_apps = wide(value);
        else if (key == "global_hook") result.global_hook = value != "0";
        else if (key == "alt_shift_shortcut") result.alt_shift_shortcut = value != "0";
    }
    return result;
}
void Store::save_settings(const Settings& settings_) {
    exec("BEGIN IMMEDIATE");
    try {
        std::map<std::string, std::string> values = {
            {"mode", settings_.mode == Mode::Automatic ? "automatic" : "cautious"},
            {"locked", settings_.locked ? "1" : "0"},
            {"bank_minimum", std::to_string(std::clamp<size_t>(settings_.bank_minimum, 1, 32))},
            {"switch_minimum", std::to_string(std::clamp<size_t>(settings_.switch_minimum, 1, 32))},
            {"excluded_apps", utf8(settings_.excluded_apps)},
            {"global_hook", settings_.global_hook ? "1" : "0"},
            {"alt_shift_shortcut", settings_.alt_shift_shortcut ? "1" : "0"}
        };
        for (const auto& [key, value] : values) {
            Statement put(db_, "INSERT OR REPLACE INTO settings VALUES(?,?)");
            put.text(1, key); put.text(2, value); put.row();
        }
        exec("UPDATE meta SET revision=revision+1; COMMIT");
    } catch (...) { exec("ROLLBACK"); throw; }
}
void Store::prefer(const std::string& signature_, const std::string& language) {
    exec("BEGIN IMMEDIATE");
    try {
        Statement put(db_, "INSERT OR REPLACE INTO preferences VALUES(?,?)");
        put.text(1, signature_); put.text(2, language); put.row();
        exec("UPDATE meta SET revision=revision+1; COMMIT");
    } catch (...) { exec("ROLLBACK"); throw; }
}
int64_t Store::add_import(const ImportBatch& batch,const std::atomic_bool* cancelled) {
    auto check_cancel=[&]{if(cancelled && cancelled->load())throw std::runtime_error("Import cancelled; no words were saved");};
    check_cancel();
    exec("BEGIN IMMEDIATE");
    try {
        Statement insert(db_, "INSERT INTO imports(source,pages,skipped,minimum) VALUES(?,?,?,?)");
        insert.text(1, utf8(batch.source)); insert.number(2, batch.pages); insert.number(3, batch.skipped_pages); insert.number(4, batch.minimum); insert.row();
        int64_t id = sqlite3_last_insert_rowid(db_);
        for (const auto& [language, words_] : batch.words) {
          check_cancel();
          auto spec=std::find_if(batch.languages.begin(),batch.languages.end(),[&](const auto& candidate){return candidate.id==language;});
          if(spec==batch.languages.end())continue;
          std::map<std::wstring,int64_t> normalized;
          for(const auto& [word,count]:words_){check_cancel();if(count>0 && spec->contains(word,batch.minimum)){
              auto& total=normalized[spec->lookup(word)];if(count>INT64_MAX-total)throw std::runtime_error("Imported word count is too large");total+=count;
          }}
          Statement put(db_, "INSERT INTO import_words VALUES(?,?,?,?)");
          for(const auto& [word,count]:normalized) {
            check_cancel();
            put.number(1, id); put.text(2, language); put.text(3, utf8(word)); put.number(4, count); put.row();put.reset();
          }
        }
        check_cancel();exec("UPDATE meta SET revision=revision+1; COMMIT");
        return id;
    } catch (...) { exec("ROLLBACK"); throw; }
}
bool Store::undo_import(int64_t id) {
    exec("BEGIN IMMEDIATE");
    try {
        Statement remove(db_, "DELETE FROM imports WHERE id=?"); remove.number(1, id); remove.row();
        bool changed = sqlite3_changes(db_) > 0;
        if (changed) exec("UPDATE meta SET revision=revision+1");
        exec("COMMIT");
        return changed;
    } catch (...) { exec("ROLLBACK"); throw; }
}
std::vector<ImportRecord> Store::imports() const {
    std::vector<ImportRecord> result;
    Statement query(db_, "SELECT i.id,i.source,COUNT(w.word),i.skipped FROM imports i LEFT JOIN import_words w ON w.import_id=i.id GROUP BY i.id ORDER BY i.id DESC");
    while (query.row()) result.push_back({query.integer(0), wide(query.str(1)), static_cast<int>(query.integer(2)), static_cast<int>(query.integer(3))});
    return result;
}
int64_t Store::revision() const { Statement query(db_, "SELECT revision FROM meta WHERE id=1"); return query.row() ? query.integer(0) : 0; }
BankSnapshot Store::snapshot() {
    exec("BEGIN");
    try {
        BankSnapshot result;
        result.revision=revision(); result.settings=settings(); result.vocabulary=vocabulary();
        exec("COMMIT"); return result;
    } catch(...) { exec("ROLLBACK"); throw; }
}
void Store::cancellation(const std::atomic_bool* cancelled) {
    sqlite3_progress_handler(db_,1000,cancelled?+[](void* value)->int {
        return static_cast<const std::atomic_bool*>(value)->load()?1:0;
    }:nullptr,const_cast<std::atomic_bool*>(cancelled));
}
}
