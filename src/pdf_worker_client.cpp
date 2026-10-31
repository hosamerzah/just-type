#include "pdf_protocol.hpp"
#include "runtime.hpp"
#include <objbase.h>

namespace ak {
ImportBatch extract_pdf_isolated(const std::filesystem::path& file,size_t minimum,const std::atomic_bool* cancelled,const std::vector<LanguageSpec>& languages){
    if(cancelled && cancelled->load())throw std::runtime_error("Import cancelled; no words were saved");
    GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Cannot create import identifier");
    wchar_t guid[40]{};StringFromGUID2(id,guid,40);
    auto directory=data_folder()/L"import-work"/guid;
    std::filesystem::create_directories(directory);
    auto output=directory/L"words.bin";
    struct Cleanup {std::filesystem::path directory,output;~Cleanup(){std::error_code error;std::filesystem::remove(output,error);std::filesystem::remove(directory,error);}} cleanup{directory,output};
    auto exe=module_folder()/L"AutoKeyboardPdfWorker.exe";
    std::wstring target=languages.size()==1?wide(languages.front().id):L"*";
    std::wstring command=L"\""+exe.wstring()+L"\" --extract \""+std::filesystem::absolute(file).wstring()+L"\" \""+output.wstring()+L"\" "+std::to_wstring(minimum)+L" "+target;
    struct Handles {HANDLE job=nullptr,process=nullptr,thread=nullptr;~Handles(){if(job)CloseHandle(job);if(process)CloseHandle(process);if(thread)CloseHandle(thread);}} handles;
    handles.job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!handles.job || !SetInformationJobObject(handles.job,JobObjectExtendedLimitInformation,&limit,sizeof(limit)))throw std::runtime_error("Cannot isolate the PDF worker");
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("Cannot start the PDF worker; repair the installation");
    handles.process=process.hProcess;handles.thread=process.hThread;
    if(!AssignProcessToJobObject(handles.job,handles.process)){TerminateProcess(handles.process,1);WaitForSingleObject(handles.process,5000);throw std::runtime_error("Windows could not isolate the PDF worker");}
    if(ResumeThread(handles.thread)==static_cast<DWORD>(-1))throw std::runtime_error("Cannot resume PDF worker");
    auto started=GetTickCount64();
    while(WaitForSingleObject(handles.process,50)==WAIT_TIMEOUT){
        if((cancelled && cancelled->load()) || GetTickCount64()-started>120000){
            TerminateJobObject(handles.job,1);WaitForSingleObject(handles.process,5000);
            throw std::runtime_error(cancelled && cancelled->load()?"Import cancelled; no words were saved":"PDF extraction timed out; no words were saved");
        }
    }
    DWORD code=1;GetExitCodeProcess(handles.process,&code);
    if(code)throw std::runtime_error("PDF extraction failed. The file may be damaged, encrypted, or disallow extraction. No words were saved.");
    if(cancelled && cancelled->load())throw std::runtime_error("Import cancelled; no words were saved");
    auto batch=pdf_protocol::read(output,minimum,languages,cancelled);batch.source=file.filename().wstring();return batch;
}
}
