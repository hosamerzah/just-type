#include "pdf_protocol.hpp"
#include "runtime.hpp"
#include "build_identity.hpp"
#include <iostream>

int wmain(int argc,wchar_t** argv){
    if(argc==2 && std::wstring(argv[1])==L"--identity"){std::cout<<AK_VERSION<<' '<<AK_SOURCE_ID<<'\n';return 0;}
    if(argc!=6 || std::wstring(argv[1])!=L"--extract")return 2;
    try {
        auto minimum=std::stoul(argv[4]);if(minimum<1 || minimum>32)return 2;
        ak::Engine engine;engine.load(ak::packs_folder());std::vector<ak::LanguageSpec> languages;
        for(const auto& pack:engine.packs)if(std::wstring(argv[5])==L"*" || ak::wide(pack.id)==argv[5])languages.push_back(pack);
        if(languages.empty())return 2;
        auto result=ak::extract_pdf(argv[2],minimum,nullptr,languages);ak::pdf_protocol::write(argv[3],result);return 0;
    }catch(const std::exception&){return 1;}
}
