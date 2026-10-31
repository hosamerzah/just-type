#pragma once
#include "pdf_import.hpp"
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <climits>

namespace ak::pdf_protocol {
inline void number(std::ostream& out,int64_t value){out.write(reinterpret_cast<const char*>(&value),sizeof(value));}
inline int64_t number(std::istream& in){int64_t value=0;in.read(reinterpret_cast<char*>(&value),sizeof(value));if(!in)throw std::runtime_error("Truncated PDF worker result");return value;}
inline void text(std::ostream& out,const std::string& value){number(out,static_cast<int64_t>(value.size()));out.write(value.data(),value.size());}
inline std::string text(std::istream& in,size_t limit=65536){auto count=number(in);if(count<0 || static_cast<uint64_t>(count)>limit)throw std::runtime_error("Invalid PDF worker result");std::string value(static_cast<size_t>(count),'\0');in.read(value.data(),count);if(!in)throw std::runtime_error("Truncated PDF worker text");return value;}
inline void write(const std::filesystem::path& path,const ImportBatch& batch){
    std::ofstream out(path,std::ios::binary|std::ios::trunc);text(out,"AKPDF1");
    number(out,batch.pages);number(out,batch.skipped_pages);number(out,batch.rejected_tokens);
    number(out,static_cast<int64_t>(batch.unique_count()));
    for(const auto& [language,words]:batch.words)for(const auto& [word,count]:words){text(out,language);text(out,utf8(word));number(out,count);}
    out.flush();if(!out)throw std::runtime_error("Cannot write PDF extraction result");
}
inline ImportBatch read(const std::filesystem::path& path,size_t minimum,const std::vector<LanguageSpec>& languages,const std::atomic_bool* cancelled=nullptr){
    if(std::filesystem::file_size(path)>256ull*1024*1024)throw std::runtime_error("PDF extraction result is too large");
    std::ifstream in(path,std::ios::binary);if(text(in)!="AKPDF1")throw std::runtime_error("Invalid PDF worker protocol");
    ImportBatch batch;batch.languages=languages;batch.minimum=minimum;
    auto bounded=[&](int64_t maximum){auto value=number(in);if(value<0 || value>maximum)throw std::runtime_error("Invalid PDF worker count");return value;};
    batch.pages=static_cast<int>(bounded(10000));batch.skipped_pages=static_cast<int>(bounded(batch.pages));batch.rejected_tokens=static_cast<int>(bounded(INT_MAX));
    auto count=bounded(1000000);
    for(int64_t i=0;i<count;++i){if(cancelled && cancelled->load())throw std::runtime_error("Import cancelled; no words were saved");auto language=text(in,32);auto word=wide(text(in));auto occurrences=bounded(INT64_MAX);
        auto spec=std::find_if(languages.begin(),languages.end(),[&](const auto& value){return value.id==language;});
        if(!occurrences || spec==languages.end() || !spec->contains(word,minimum) || spec->lookup(word)!=word || batch.words[language].contains(word))throw std::runtime_error("Invalid extracted vocabulary");
        batch.words[language].emplace(std::move(word),occurrences);
    }
    if(in.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Unexpected PDF worker data");return batch;
}
}
