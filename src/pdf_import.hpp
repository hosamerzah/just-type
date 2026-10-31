#pragma once
#include "store.hpp"
#include <atomic>
namespace ak {
ImportBatch extract_pdf(const std::filesystem::path& file, size_t minimum = 4, const std::atomic_bool* cancelled = nullptr, const std::vector<LanguageSpec>& languages = default_languages());
ImportBatch extract_text(std::wstring_view text, size_t minimum = 4, const std::vector<LanguageSpec>& languages = default_languages());
ImportBatch extract_pdf_isolated(const std::filesystem::path& file,size_t minimum,const std::atomic_bool* cancelled,const std::vector<LanguageSpec>& languages);
}
