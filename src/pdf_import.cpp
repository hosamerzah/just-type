#include "pdf_import.hpp"
#include <fpdfview.h>
#include <fpdf_text.h>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace ak {
ImportBatch extract_text(std::wstring_view text, size_t minimum, const std::vector<LanguageSpec>& languages) {
    ImportBatch batch;
    batch.languages=languages;
    batch.minimum = minimum;
    for (const auto& raw : tokenize(text)) {
        bool accepted=false;
        for(const auto& language:languages)if(language.contains(raw,minimum)) {
            ++batch.words[language.id][language.lookup(raw)]; accepted=true; break;
        }
        if(!accepted)++batch.rejected_tokens;
    }
    return batch;
}
ImportBatch extract_pdf(const std::filesystem::path& file, size_t minimum, const std::atomic_bool* cancelled, const std::vector<LanguageSpec>& languages) {
    // PDFium global initialization and access are serialized inside this worker
    // library. It is never linked into the TSF input DLL.
    static std::mutex mutex;
    std::lock_guard guard(mutex);
    struct Library { Library() { FPDF_InitLibrary(); } ~Library() { FPDF_DestroyLibrary(); } } library;
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open the selected PDF");
    auto length = input.tellg();
    if (length <= 0 || length > 256 * 1024 * 1024) throw std::runtime_error("Choose a PDF smaller than 256 MB");
    std::vector<char> bytes(static_cast<size_t>(length));
    input.seekg(0); input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("Cannot read the selected PDF");
    struct Document { FPDF_DOCUMENT value; ~Document() { if (value) FPDF_CloseDocument(value); } } document{FPDF_LoadMemDocument64(bytes.data(), bytes.size(), nullptr)};
    if (!document.value) {
        if (FPDF_GetLastError() == FPDF_ERR_PASSWORD) throw std::runtime_error("This PDF is password protected; import an unlocked copy");
        throw std::runtime_error("Cannot read this PDF (damaged or unsupported file)");
    }
    if ((FPDF_GetDocPermissions(document.value) & (1u << 4)) == 0) throw std::runtime_error("This PDF does not permit text extraction");
    ImportBatch batch;
    batch.languages=languages;
    batch.minimum = minimum;
    batch.source = file.filename().wstring();
    batch.pages = FPDF_GetPageCount(document.value);
    if (batch.pages > 10000) throw std::runtime_error("Choose a PDF with no more than 10,000 pages");
    for (int i = 0; i < batch.pages; ++i) {
        if (cancelled && cancelled->load()) throw std::runtime_error("Import cancelled");
        struct Page { FPDF_PAGE value; ~Page() { if (value) FPDF_ClosePage(value); } } page{FPDF_LoadPage(document.value, i)};
        if (!page.value) throw std::runtime_error("Cannot load a PDF page; no words were imported");
        struct Text { FPDF_TEXTPAGE value; ~Text() { if (value) FPDFText_ClosePage(value); } } text{FPDFText_LoadPage(page.value)};
        if (!text.value) throw std::runtime_error("Cannot extract a PDF page; no words were imported");
        int count = FPDFText_CountChars(text.value);
        if (count <= 0) { ++batch.skipped_pages; continue; }
        if (count > 2000000) throw std::runtime_error("A PDF page exceeds the text extraction limit");
        std::wstring buffer(static_cast<size_t>(count) + 1, 0);
        int copied = FPDFText_GetText(text.value, 0, count, reinterpret_cast<unsigned short*>(buffer.data()));
        if (copied <= 1) { ++batch.skipped_pages; continue; }
        buffer.resize(copied - 1);
        // Skip whitespace-only and non-text pages, not merely pages whose words
        // were filtered out for being short.
        if (letter_count(buffer) == 0) { ++batch.skipped_pages; continue; }
        auto pageBatch = extract_text(buffer, minimum, languages);
        batch.rejected_tokens += pageBatch.rejected_tokens;
        for (const auto& [lang, words] : pageBatch.words) for (const auto& [word, n] : words) batch.words[lang][word] += n;
        if (batch.unique_count() > 1000000) throw std::runtime_error("PDF contains more than one million unique words");
    }
    return batch;
}
}
