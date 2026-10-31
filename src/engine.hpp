#pragma once
#include "text.hpp"
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Hunspell;
namespace ak {
enum class Mode { Cautious, Automatic };
struct Stroke { unsigned key{}; bool shift{}, caps{}; };
struct Mapping { std::wstring normal, shifted; };
struct LanguagePack : public LanguageSpec {
    std::wstring name;
    bool caps = false;
    std::unordered_map<unsigned, Mapping> keys;
    std::unordered_map<std::wstring, int> frequency;
    std::unique_ptr<Hunspell> dictionary;
    LanguagePack();
    ~LanguagePack();
    LanguagePack(LanguagePack&&) noexcept;
    LanguagePack& operator=(LanguagePack&&) noexcept;
    std::wstring render(const std::vector<Stroke>& strokes) const;
    int frequency_rank(std::wstring_view word) const;
    bool accepts(std::wstring_view word, size_t minimum = 4) const;
};
struct Vocabulary {
    std::unordered_map<std::string, std::unordered_map<std::wstring, int64_t>> counts;
    std::unordered_map<std::string, std::string> preferences;
};
struct Decision { std::wstring text; std::string language; bool changed = false; std::string reason; };
class Engine {
public:
    std::vector<LanguagePack> packs;
    Vocabulary vocabulary;
    std::shared_ptr<const Vocabulary> snapshot_vocabulary;
    std::unordered_set<std::wstring> protected_words;
    std::string current = "en";
    Mode mode = Mode::Cautious;
    bool locked = false;
    size_t bank_minimum = 4;
    size_t switch_minimum = 4;
    void load(const std::filesystem::path& folder);
    void load_protected_words(const std::filesystem::path& file);
    bool is_protected(std::wstring_view word) const;
    std::wstring render(const std::vector<Stroke>& strokes, const std::string& language) const;
    bool mapped(unsigned key) const;
    bool boundary(const Stroke& stroke) const;
    Decision decide(const std::vector<Stroke>& strokes) const;
    Decision manual(const std::vector<Stroke>& strokes, const std::string& from) const;
    static std::string signature(const std::vector<Stroke>& strokes);
};
}
