#include "text.hpp"
#include <windows.h>
#include <stdexcept>

namespace ak {
std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("Invalid Unicode text");
    std::string result(count, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}
std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!count) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(count, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}
bool letter(wchar_t ch) {
    WORD type = 0;
    GetStringTypeW(CT_CTYPE1, &ch, 1, &type);
    return (type & C1_ALPHA) != 0 && !mark(ch);
}
bool mark(wchar_t ch) {
    WORD type = 0;
    GetStringTypeW(CT_CTYPE3, &ch, 1, &type);
    return (type & (C3_NONSPACING | C3_DIACRITIC | C3_VOWELMARK)) != 0;
}
std::wstring normalize(std::wstring_view word, bool arabic_marks, bool lowercase) {
    if (word.empty()) return {};
    int count = NormalizeString(NormalizationKC, word.data(), static_cast<int>(word.size()), nullptr, 0);
    if (count <= 0) throw std::runtime_error("Cannot normalize text");
    std::wstring expanded(count, 0);
    count = NormalizeString(NormalizationKC, word.data(), static_cast<int>(word.size()), expanded.data(), count);
    if (count <= 0) throw std::runtime_error("Cannot normalize text");
    expanded.resize(count);
    std::wstring result;
    for (wchar_t ch : expanded) {
        // Arabic marks and elongation are lookup-only changes. Hamza is preserved.
        if (arabic_marks && (ch == 0x0640 || ((ch >= 0x0600 && ch <= 0x08ff) && mark(ch)))) continue;
        result += ch;
    }
    if (lowercase && !result.empty()) {
        int size = LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,result.data(),static_cast<int>(result.size()),nullptr,0,nullptr,nullptr,0);
        if (size > 0) {
            std::wstring folded(size,0);
            LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,result.data(),static_cast<int>(result.size()),folded.data(),size,nullptr,nullptr,0);
            result = std::move(folded);
        }
    }
    return result;
}
size_t letter_count(std::wstring_view word) {
    size_t count = 0;
    for (wchar_t ch : normalize(word)) if (letter(ch)) ++count;
    return count;
}
std::string script(std::wstring_view word) {
    std::string result;
    for (wchar_t ch : normalize(word)) {
        if (!letter(ch)) {
            if (mark(ch)) continue;
            return {};
        }
        std::string current;
        if (ch >= L'a' && ch <= L'z') current = "en";
        else if ((ch >= 0x0620 && ch <= 0x063f) || (ch >= 0x0641 && ch <= 0x064a) ||
                 (ch >= 0x066e && ch <= 0x06d3) || (ch >= 0x06fa && ch <= 0x06fc)) current = "ar";
        else return {};
        if (!result.empty() && result != current) return {};
        result = current;
    }
    return result;
}
bool eligible(std::wstring_view word, size_t minimum) {
    return letter_count(word) >= minimum && !script(word).empty();
}
std::vector<std::wstring> tokenize(std::wstring_view text) {
    std::vector<std::wstring> result;
    std::wstring token;
    auto flush = [&] { if (!token.empty()) result.push_back(std::move(token)); token.clear(); };
    // Keep numbers and connectors in a token so "hello123"/"hello_world"
    // are rejected rather than importing their seemingly valid fragments.
    for (wchar_t ch : text) {
        WORD type = 0;
        GetStringTypeW(CT_CTYPE1, &ch, 1, &type);
        if (letter(ch) || mark(ch) || ch == 0x0640 || (type & C1_DIGIT) || ch == L'_' || ch == L'\'' || ch == L'\u2019' ||
            (ch >= 0xfb50 && ch <= 0xfdff) || (ch >= 0xfe70 && ch <= 0xfeff)) token += ch;
        else flush();
    }
    flush();
    return result;
}
std::wstring LanguageSpec::lookup(std::wstring_view word) const {
    auto result=normalize(word,normalization=="arabic",normalization!="nfkc");
    if(connectors.find(L'\'')!=std::wstring::npos && connectors.find(L'\u2019')!=std::wstring::npos)
        for(auto& ch:result)if(ch==L'\u2019')ch=L'\'';
    return result;
}
bool LanguageSpec::contains(std::wstring_view word, size_t minimum) const {
    auto normalized=lookup(word);
    size_t letters=0;
    bool previousLetter=false;
    for(size_t i=0;i<normalized.size();++i) {
        wchar_t ch=normalized[i];
        if(mark(ch))continue;
        if(!letter(ch)){
            if(connectors.find(ch)!=std::wstring::npos && previousLetter && i+1<normalized.size() && letter(normalized[i+1])){previousLetter=false;continue;}
            return false;
        }
        bool allowed=false;
        for(auto [first,last]:ranges)if(static_cast<unsigned>(ch)>=first && static_cast<unsigned>(ch)<=last){allowed=true;break;}
        if(!allowed)return false;
        ++letters;
        previousLetter=true;
    }
    return letters>=minimum;
}
std::vector<LanguageSpec> default_languages() {
    return {{"en","casefold",{{0x61,0x7a}},L"'\u2019"},{"ar","arabic",{{0x620,0x63f},{0x641,0x64a},{0x66e,0x6d3},{0x6fa,0x6fc}},L""}};
}
}
