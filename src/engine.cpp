#include "engine.hpp"
#include <hunspell.hxx>
#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace ak {
namespace {
std::wstring fold_arabic_hamza(std::wstring_view word) {
    std::wstring result;
    result.reserve(word.size());
    for (wchar_t ch : word) {
        if (ch == 0x0622 || ch == 0x0623 || ch == 0x0625) ch = 0x0627;
        result += ch;
    }
    return result;
}
}
LanguagePack::LanguagePack() = default;
LanguagePack::~LanguagePack() = default;
LanguagePack::LanguagePack(LanguagePack&&) noexcept = default;
LanguagePack& LanguagePack::operator=(LanguagePack&&) noexcept = default;
std::wstring LanguagePack::render(const std::vector<Stroke>& strokes) const {
    std::wstring result;
    for (const auto& stroke : strokes) {
        auto it = keys.find(stroke.key);
        if (it == keys.end()) return {};
        bool shifted = stroke.shift;
        if (caps && stroke.key >= 0x41 && stroke.key <= 0x5a) shifted ^= stroke.caps;
        result += shifted ? it->second.shifted : it->second.normal;
    }
    return result;
}
int LanguagePack::frequency_rank(std::wstring_view word) const {
    auto normalized = lookup(word);
    if (auto it = frequency.find(normalized); it != frequency.end()) return it->second;
    if (normalization == "arabic") {
        if (auto it = frequency.find(fold_arabic_hamza(normalized)); it != frequency.end()) return it->second;
    }
    return std::numeric_limits<int>::max();
}
bool LanguagePack::accepts(std::wstring_view word, size_t minimum) const {
    auto normalized = lookup(word);
    if (!contains(normalized, minimum)) return false;
    if (dictionary && dictionary->spell(utf8(normalized))) return true;
    return frequency_rank(normalized) <= 20000;
}
void Engine::load(const std::filesystem::path& folder) {
    packs.clear();
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(folder))
        if (entry.is_directory() && std::filesystem::exists(entry.path() / "pack.ini")) paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    // English is the default tie-break pack; every other pack follows by ID.
    std::stable_sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.filename() == "en" && b.filename() != "en"; });
    for (const auto& path : paths) {
        LanguagePack pack;
        std::string aff, dic;
        std::ifstream manifest(path / "pack.ini");
        std::string line;
        while (std::getline(manifest, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto split = line.find('=');
            if (split == std::string::npos) continue;
            auto key = line.substr(0, split), value = line.substr(split + 1);
            if (key == "id") pack.id = value;
            else if (key == "name") pack.name = wide(value);
            else if (key == "caps") pack.caps = value == "true";
            else if (key == "aff") aff = value;
            else if (key == "dic") dic = value;
            else if (key == "normalization") pack.normalization = value;
            else if(key=="connectors"){
                std::istringstream input(value);std::string code;
                while(std::getline(input,code,',')){size_t used=0;auto ch=std::stoul(code,&used,16);
                    if(used!=code.size() || (ch!=0x27 && ch!=0x2019))throw std::runtime_error("Unsupported word connector");
                    pack.connectors+=static_cast<wchar_t>(ch);
                }
            }
            else if (key == "ranges") {
                std::istringstream input(value); std::string range;
                while(std::getline(input,range,',')) {
                    auto dash=range.find('-');
                    if(dash==std::string::npos)throw std::runtime_error("Invalid language character range");
                    unsigned first=std::stoul(range.substr(0,dash),nullptr,16),last=std::stoul(range.substr(dash+1),nullptr,16);
                    if(first>last || last>0xffff)throw std::runtime_error("Language packs must use valid BMP character ranges");
                    pack.ranges.emplace_back(first,last);
                }
            }
        }
        if(pack.ranges.empty())for(const auto& spec:default_languages())if(spec.id==pack.id){pack.ranges=spec.ranges;pack.normalization=spec.normalization;}
        if (pack.id.empty() || pack.id.size()>32 || pack.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_")!=std::string::npos || pack.name.empty() || pack.ranges.empty() || aff.empty() || dic.empty() ||
            (pack.normalization!="arabic" && pack.normalization!="casefold" && pack.normalization!="nfkc") ||
            std::filesystem::path(aff).filename() != std::filesystem::path(aff) ||
            std::filesystem::path(dic).filename() != std::filesystem::path(dic)) throw std::runtime_error("Invalid language pack manifest");
        std::ifstream keys(path / "keys.tsv");
        while (std::getline(keys, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            auto first = line.find('\t'), second = line.find('\t', first + 1);
            if (first == std::string::npos || second == std::string::npos) throw std::runtime_error("Invalid key map");
            size_t used=0;auto key=std::stoul(line.substr(0,first),&used,16);
            auto normal=wide(line.substr(first+1,second-first-1)),shifted=wide(line.substr(second+1));
            if(used!=first || key==0 || key>255 || normal.empty() || shifted.empty() || normal.size()>8 || shifted.size()>8 || pack.keys.contains(key))
                throw std::runtime_error("Invalid or duplicate keyboard mapping in "+path.filename().string());
            pack.keys.emplace(static_cast<unsigned>(key),Mapping{std::move(normal),std::move(shifted)});
        }
        if (pack.keys.empty()) throw std::runtime_error("Empty key map");
        // Hunspell's Windows file helper understands long UTF-8 paths prefixed by \\?\.
        const auto affPath = utf8(std::filesystem::absolute(path / aff).wstring());
        const auto dicPath = utf8(std::filesystem::absolute(path / dic).wstring());
        if (!std::filesystem::is_regular_file(path / aff) || !std::filesystem::is_regular_file(path / dic)) throw std::runtime_error("Dictionary file is missing or is not a regular file");
        pack.dictionary = std::make_unique<Hunspell>(affPath.c_str(), dicPath.c_str());
        if (std::string(pack.dictionary->get_dic_encoding()) != "UTF-8") throw std::runtime_error("Packs must use UTF-8 dictionaries");
        if (std::filesystem::is_regular_file(path / "frequency.txt")) {
            std::ifstream freq(path / "frequency.txt");
            int rank = 0;
            while (std::getline(freq, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line[0] == '#') continue;
                auto space = line.find_first_of(" \t");
                auto token = space == std::string::npos ? line : line.substr(0, space);
                if (token.empty()) continue;
                try {
                    auto word = pack.lookup(wide(token));
                    if (!pack.contains(word, 1)) continue;
                    ++rank;
                    pack.frequency.try_emplace(word, rank);
                    if (pack.normalization == "arabic") pack.frequency.try_emplace(fold_arabic_hamza(word), rank);
                } catch (...) { }
            }
        }
        for (const auto& existing : packs) if (existing.id == pack.id) throw std::runtime_error("Duplicate language ID");
        packs.push_back(std::move(pack));
    }
    if (packs.size() < 2) throw std::runtime_error("At least two language packs are required");
    if (std::none_of(packs.begin(), packs.end(), [&](const auto& p) { return p.id == current; })) current = packs.front().id;
}
void Engine::load_protected_words(const std::filesystem::path& file) {
    protected_words.clear();
    if (!std::filesystem::is_regular_file(file)) return;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
        auto begin = line.find_first_not_of(" \t"), end = line.find_last_not_of(" \t");
        if (begin == std::string::npos || line[begin] == '#') continue;
        try {
            auto word = normalize(wide(line.substr(begin, end - begin + 1)));
            if (!word.empty()) protected_words.insert(std::move(word));
        } catch (...) { }
    }
}
bool Engine::is_protected(std::wstring_view word) const {
    if (protected_words.empty() || word.empty()) return false;
    try { return protected_words.contains(normalize(word)); } catch (...) { return false; }
}
std::wstring Engine::render(const std::vector<Stroke>& strokes, const std::string& language) const {
    for (const auto& pack : packs) if (pack.id == language) return pack.render(strokes);
    return {};
}
bool Engine::mapped(unsigned key) const {
    return !packs.empty() && std::all_of(packs.begin(), packs.end(), [&](const auto& pack) { return pack.keys.contains(key); });
}
bool Engine::boundary(const Stroke& stroke) const {
    if (stroke.key == 0x20 || stroke.key == 0x0d) return true;
    if (!mapped(stroke.key)) return false;
    for (const auto& pack : packs) {
        for (wchar_t ch : pack.render({stroke})) if (letter(ch) || mark(ch)) return false;
    }
    return true;
}
std::string Engine::signature(const std::vector<Stroke>& strokes) {
    std::ostringstream result;
    for (const auto& s : strokes) result << std::hex << s.key << ':' << s.shift << ':' << s.caps << ';';
    return result.str();
}
Decision Engine::decide(const std::vector<Stroke>& strokes) const {
    const auto& vocabulary = snapshot_vocabulary ? *snapshot_vocabulary : this->vocabulary;
    Decision keep{render(strokes, current), current, false, "unchanged"};
    if (locked) { keep.reason = "locked"; return keep; }
    if (is_protected(keep.text)) { keep.reason = "protected"; return keep; }
    struct Candidate { std::wstring text; std::string language; int64_t count; int rank; bool preferred; };
    std::vector<Candidate> valid;
    auto preferred = vocabulary.preferences.find(signature(strokes));
    for (const auto& pack : packs) {
        auto text = pack.render(strokes), word = pack.lookup(text);
        if (!pack.contains(word, std::max(bank_minimum, switch_minimum))) continue;
        int64_t count = 0;
        if (auto lang = vocabulary.counts.find(pack.id); lang != vocabulary.counts.end())
            if (auto it = lang->second.find(word); it != lang->second.end()) count = it->second;
        int rank = pack.frequency_rank(word);
        if (count > 0 || pack.accepts(word, bank_minimum))
            valid.push_back({text, pack.id, count, rank, preferred != vocabulary.preferences.end() && preferred->second == pack.id});
    }
    if (valid.empty()) { keep.reason = letter_count(keep.text)<switch_minimum ? "short-or-malformed" : "unknown"; return keep; }
    if (mode == Mode::Cautious && valid.size() != 1) { keep.reason = "ambiguous"; return keep; }
    std::stable_sort(valid.begin(), valid.end(), [&](const auto& a, const auto& b) {
        if (a.preferred != b.preferred) return a.preferred;
        if (a.count != b.count) return a.count > b.count;
        if (a.count == 0 && b.count == 0 && a.rank != b.rank) return a.rank < b.rank;
        return a.language == current && b.language != current;
    });
    const auto& chosen = valid.front();
    return {chosen.text, chosen.language, chosen.language != current, "dictionary"};
}
Decision Engine::manual(const std::vector<Stroke>& strokes, const std::string& from) const {
    auto it = std::find_if(packs.begin(), packs.end(), [&](const auto& p) { return p.id == from; });
    size_t index = it == packs.end() ? 0 : (static_cast<size_t>(it - packs.begin()) + 1) % packs.size();
    return {packs[index].render(strokes), packs[index].id, packs[index].id != from, "manual"};
}
}
