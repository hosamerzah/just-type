#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace ak {
std::string utf8(std::wstring_view value);
std::wstring wide(std::string_view value);
std::wstring normalize(std::wstring_view word, bool arabic_marks = true, bool lowercase = true);
bool letter(wchar_t ch);
bool mark(wchar_t ch);
size_t letter_count(std::wstring_view word);
bool eligible(std::wstring_view word, size_t minimum = 4);
std::string script(std::wstring_view word);
std::vector<std::wstring> tokenize(std::wstring_view text);
struct LanguageSpec {
    std::string id;
    std::string normalization = "casefold";
    std::vector<std::pair<unsigned, unsigned>> ranges;
    std::wstring connectors = {};
    std::wstring lookup(std::wstring_view word) const;
    bool contains(std::wstring_view word, size_t minimum = 1) const;
};
std::vector<LanguageSpec> default_languages();
}
