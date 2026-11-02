#include "engine.hpp"
#include "store.hpp"
#include "pdf_import.hpp"
#include "runtime.hpp"
#include "global_hook.hpp"
#include <windows.h>
#include "win_compat.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <functional>
#include <thread>
#include <chrono>
#include <algorithm>
#include "pdf_protocol.hpp"
#include "diagnostics.hpp"

using namespace ak;
namespace {
struct Environment {
    const wchar_t* name;std::wstring previous;
    Environment(const wchar_t* key,const wchar_t* value):name(key){wchar_t buffer[32768]{};auto count=GetEnvironmentVariableW(key,buffer,32768);if(count&&count<32768)previous.assign(buffer,count);SetEnvironmentVariableW(key,value);}
    ~Environment(){SetEnvironmentVariableW(name,previous.empty()?nullptr:previous.c_str());}
};
int passed = 0, failed = 0;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void test(const char* name, const std::function<void()>& action) {
    try { action(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
}
std::vector<Stroke> keys(const std::string& text) {
    std::vector<Stroke> result;
    for (unsigned char ch : text) {
        if (ch >= 'a' && ch <= 'z') result.push_back({static_cast<unsigned>(ch - 'a' + 'A'),false,false});
        else if (ch >= 'A' && ch <= 'Z') result.push_back({ch,true,false});
        else throw std::runtime_error("Test helper needs letters");
    }
    return result;
}
void make_pdf(const std::filesystem::path& file) {
    std::vector<std::string> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Count 3 /Kids [3 0 R 4 0 R 5 0 R] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /Font << /F1 6 0 R >> >> /Contents 7 0 R >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << >> >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /Font << /F1 8 0 R >> >> /Contents 11 0 R >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "",
        "<< /Type /Font /Subtype /Type0 /BaseFont /Arial /Encoding /Identity-H /DescendantFonts [9 0 R] /ToUnicode 10 0 R >>",
        "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Arial /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /DW 500 >>",
        "",
        ""
    };
    auto stream = [](const std::string& content) { return "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream"; };
    objects[6] = stream("BT /F1 12 Tf 50 700 Td (hello hello keyboard cat a) Tj ET");
    objects[9] = stream("/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def /CMapName /Identity-UCS def /CMapType 2 def 1 begincodespacerange <0000> <FFFF> endcodespacerange 1 beginbfrange <0000> <FFFF> <0000> endbfrange endcmap CMapName currentdict /CMap defineresource pop end end");
    // PDF glyphs are placed left-to-right in visual order. PDFium returns the
    // logical Arabic reading order from these positioned glyphs.
    objects[10] = stream("BT /F1 12 Tf 50 700 Td <064506270644063306440627> Tj ET");
    std::ostringstream out; out << "%PDF-1.7\n";
    std::vector<size_t> offsets{0};
    for (size_t i=0; i<objects.size(); ++i) { offsets.push_back(static_cast<size_t>(out.tellp())); out << i+1 << " 0 obj\n" << objects[i] << "\nendobj\n"; }
    auto xref = out.tellp();
    out << "xref\n0 " << objects.size()+1 << "\n0000000000 65535 f \n";
    for (size_t i=1; i<offsets.size(); ++i) { char row[32]; snprintf(row,sizeof(row),"%010llu 00000 n \n",static_cast<unsigned long long>(offsets[i])); out << row; }
    out << "trailer\n<< /Size " << objects.size()+1 << " /Root 1 0 R >>\nstartxref\n" << xref << "\n%%EOF\n";
    std::ofstream(file,std::ios::binary) << out.str();
}
}
int main() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Engine engine;
    try { engine.load(std::filesystem::current_path() / "packs"); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    auto temporary = std::filesystem::temp_directory_path() / (L"AutoKeyboard-tests-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(temporary);
    test("Arabic physical mapping",[&]{ check(engine.render(keys("hgsghl"),"ar") == L"السلام","incorrect Arabic mapping"); });
    test("Arabic lam-alef expands to two letters",[&]{ check(engine.render(keys("b"),"ar") == L"لا","lam-alef lost"); check(letter_count(L"ﻻسلام") == 6,"ligature count"); });
    test("Arabic lookup marks and tatweel",[]{ check(normalize(L"السَّــلام") == L"السلام","normalization"); check(letter_count(L"مَنْ") == 2,"marks counted as letters"); });
    test("Hamza distinctions preserved",[]{ check(normalize(L"أحمد") != normalize(L"احمد"),"hamza merged"); });
    test("English case preserves displayed spelling",[&]{ check(engine.render(keys("Hello"),"en") == L"Hello","case lost"); check(normalize(L"Hello") == L"hello","lookup case"); });
    test("Caps Lock only changes Latin letters",[&]{ auto s=keys("hello"); for(auto& k:s)k.caps=true; check(engine.render(s,"en")==L"HELLO","caps"); check(engine.render(s,"ar")==engine.render(keys("hello"),"ar"),"caps changed Arabic"); });
    test("Bundled English dictionary and affix rules",[&]{ auto& en=engine.packs[0]; check(en.id=="en","pack order"); check(en.accepts(L"keyboard"),"missing word"); check(en.accepts(L"keyboards"),"plural affix not accepted"); });
    test("Bundled Arabic dictionary",[&]{ check(engine.packs[1].accepts(L"السلام"),"Arabic dictionary missing السلام"); });
    test("English contractions retain dictionary coverage and letter counts",[&]{
        check(engine.packs[0].accepts(L"can't",4),"valid contraction excluded");
        check(!engine.packs[0].accepts(L"can't",5),"apostrophe counted as a letter");
        auto batch=extract_text(L"don't don’t hello_world don''t",4);
        check(batch.unique_count()==1 && batch.words["en"][L"don't"]==2,"contractions or malformed connector filtering incorrect");
    });
    test("Cautious Arabic conversion",[&]{ engine.current="en"; auto d=engine.decide(keys("hgsghl")); check(d.text==L"السلام" && d.changed,"did not convert"); });
    test("Arabic words whose English keys include punctuation",[&]{engine.current="en";std::vector<Stroke> s{{0xba,false,false},{'J',false,false},{'H',false,false},{'F',false,false}};auto d=engine.decide(s);check(d.text==L"كتاب" && d.language=="ar","Arabic letter on punctuation key blocked conversion");});
    test("Cautious English conversion",[&]{ engine.current="ar"; auto d=engine.decide(keys("hello")); check(d.text==L"hello" && d.language=="en","did not switch to English"); });
    test("Unknown words remain unchanged",[&]{ engine.current="en"; auto d=engine.decide(keys("zxqvzxqv")); check(d.text==L"zxqvzxqv" && !d.changed,"unknown converted"); });
    test("Default short words never switch",[&]{ engine.current="ar"; auto d=engine.decide(keys("cat")); check(d.language=="ar" && d.reason=="short-or-malformed","short word switched"); });
    test("Optional bank filter includes short words",[&]{ check(!engine.packs[0].accepts(L"cat",4),"short word accepted with filter"); check(engine.packs[0].accepts(L"cat",1),"short word unavailable when filter off"); });
    test("Switch minimum independent of bank filter",[&]{ engine.current="ar"; engine.bank_minimum=1; check(!engine.decide(keys("cat")).changed,"bank setting bypassed switch setting"); engine.switch_minimum=1; check(engine.decide(keys("cat")).language=="en","short switching not enabled"); engine.bank_minimum=engine.switch_minimum=4; });
    test("Cautious ambiguous word stays",[&]{ engine.current="en"; auto s=keys("hello"); engine.vocabulary.counts["ar"][normalize(engine.render(s,"ar"))]=5; check(!engine.decide(s).changed,"ambiguous word changed"); });
    test("Automatic mode ranks document frequency",[&]{ engine.mode=Mode::Automatic; auto d=engine.decide(keys("hello")); check(d.language=="ar","frequency ignored"); });
    test("Personal preference outranks frequency",[&]{ auto s=keys("hello"); engine.vocabulary.preferences[Engine::signature(s)]="en"; check(engine.decide(s).language=="en","preference ignored"); engine.vocabulary={}; engine.mode=Mode::Cautious; });
    test("Tie retains current language",[&]{ engine.mode=Mode::Automatic; engine.current="ar"; auto s=keys("hello"); engine.vocabulary.counts["ar"][normalize(engine.render(s,"ar"))]=1; engine.vocabulary.counts["en"][L"hello"]=1; check(engine.decide(s).language=="ar","tie did not retain current language"); engine.vocabulary={}; engine.mode=Mode::Cautious; });
    test("Locked language does not switch",[&]{ engine.current="en"; engine.locked=true; check(!engine.decide(keys("hgsghl")).changed,"locked changed"); engine.locked=false; });
    test("Manual conversion ignores length and dictionaries",[&]{ check(engine.manual(keys("cat"),"ar").text==L"cat","short manual conversion failed"); });
    test("Punctuation keys that are Arabic letters are not boundaries",[&]{ check(!engine.boundary({0xbe,false,false}),"period is Arabic letter"); check(!engine.boundary({0xde,false,false}),"apostrophe is Arabic letter"); check(engine.boundary({0x31,true,false}),"exclamation boundary missing"); check(engine.boundary({0x20,false,false}),"space boundary missing"); });
    test("Mixed scripts and numbers rejected",[]{ auto b=extract_text(L"hello123 hello_world testعربي 1234 ١٢٣٤ hello",4); check(b.unique_count()==1 && b.words["en"][L"hello"]==1,"malformed tokens imported"); });
    test("Text import normalizes and deduplicates",[]{ auto b=extract_text(L"Hello HELLO hello السَّلام السلام cat من",4); check(b.words["en"][L"hello"]==3,"English counts"); check(b.words["ar"][L"السلام"]==2,"Arabic counts"); check(b.unique_count()==2,"short words imported"); });
    test("Text import optional minimum",[]{ auto b=extract_text(L"cat من a",1); check(b.unique_count()==3,"optional length filter not applied"); });
    test("Additional script pack import and normalization",[&]{LanguageSpec ru{"ru","casefold",{{0x430,0x44f}}};auto b=extract_text(L"ПРИВЕТ привет abc",4,{ru});check(b.words["ru"][L"привет"]==2 && b.unique_count()==1,"custom script routing");Store db(temporary/L"russian.db");db.add_import(b);check(db.vocabulary().counts["ru"][L"привет"]==2,"custom script persistence");});
    test("Additional direct-layout candidate participates in decisions",[&]{LanguagePack p;p.id="ru";p.normalization="casefold";p.ranges={{0x430,0x44f}};p.keys=engine.packs[0].keys;p.keys['H']={L"п",L"П"};p.keys['Q']={L"д",L"Д"};p.keys['Z']={L"а",L"А"};engine.packs.push_back(std::move(p));engine.vocabulary.counts["ru"][L"пдаа"]=1;engine.current="en";check(engine.decide(keys("hqzz")).language=="ru","extra pack ignored");engine.packs.pop_back();engine.vocabulary={};});
    test("PDF text, Arabic, and image-only-page skipping",[&]{ make_pdf(temporary/L"mixed.pdf"); auto b=extract_pdf(temporary/L"mixed.pdf"); check(b.pages==3 && b.skipped_pages==1,"page counts"); check(b.words["en"][L"hello"]==2,"PDF English count"); check(b.words["ar"][L"السلام"]==1,"PDF Arabic extraction"); check(b.unique_count()==3,"PDF filters"); });
    test("PDF optional minimum",[&]{ auto b=extract_pdf(temporary/L"mixed.pdf",1); check(b.words["en"].contains(L"cat") && b.words["en"].contains(L"a"),"short PDF words unavailable"); });
    test("Malformed PDF reports failure",[&]{ std::ofstream(temporary/L"bad.pdf") << "not a pdf"; bool threw=false; try{extract_pdf(temporary/L"bad.pdf");}catch(...){threw=true;} check(threw,"bad PDF accepted"); });
    test("Cancelled PDF produces no batch",[&]{ std::atomic_bool cancel{true}; bool threw=false; try{extract_pdf(temporary/L"mixed.pdf",4,&cancel);}catch(...){threw=true;} check(threw,"cancellation ignored"); });
    test("PDF worker extracts outside the settings process",[&]{
        SetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",temporary.c_str());
        auto batch=extract_pdf_isolated(temporary/L"mixed.pdf",4,nullptr,default_languages());
        check(batch.pages==3 && batch.skipped_pages==1 && batch.words["en"][L"hello"]==2,"isolated PDF result changed");
        SetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",nullptr);
    });
    test("PDF worker protocol rejects malformed vocabulary",[&]{
        auto batch=extract_text(L"hello hello",4);batch.pages=1;
        pdf_protocol::write(temporary/L"worker.bin",batch);
        auto result=pdf_protocol::read(temporary/L"worker.bin",4,default_languages());check(result.words["en"][L"hello"]==2,"protocol lost counts");
        std::ofstream(temporary/L"worker.bin",std::ios::app)<<"trailing data";
        bool rejected=false;try{pdf_protocol::read(temporary/L"worker.bin",4,default_languages());}catch(...){rejected=true;}check(rejected,"malformed worker data accepted");
    });
    test("Cancelled database import preserves revision and history",[&]{
        Store db(temporary/L"cancel.db");auto batch=extract_text(L"hello keyboard",4);std::atomic_bool cancelled{true};
        bool rejected=false;try{db.add_import(batch,&cancelled);}catch(...){rejected=true;}
        check(rejected && db.revision()==0 && db.imports().empty() && db.vocabulary().counts.empty(),"cancelled transaction saved words");
    });
    test("Background snapshots retain an immutable complete revision",[&]{
        auto path=temporary/L"snapshot.db";Store db(path);db.save_settings(Settings{});
        SnapshotLoader loader;loader.start(path);auto first=loader.latest(true);check(first && first->revision==1,"initial snapshot missing");
        db.add_import(extract_text(L"hello hello",4));std::shared_ptr<const BankSnapshot> next;
        for(int i=0;i<500;++i){next=loader.latest();if(next && next->revision==2)break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        check(next && next->revision==2 && next->vocabulary.counts.at("en").at(L"hello")==2,"new snapshot inconsistent");
        check(first->revision==1 && first->vocabulary.counts.empty(),"immutable snapshot changed");loader.stop();
    });
    test("Large bank refresh does not block in-memory decisions",[&]{
        ImportBatch batch;batch.minimum=1;
        for(unsigned i=0;i<100000;++i){std::wstring word=L"fixture";unsigned n=i;do{word+=static_cast<wchar_t>(L'a'+n%26);n/=26;}while(n);batch.words["en"][word]=1;}
        Store db(temporary/L"large.db");db.add_import(batch);
        SnapshotLoader loader;loader.start(temporary/L"large.db");
        std::vector<double> timings;
        for(int i=0;i<2000;++i){auto start=std::chrono::steady_clock::now();auto result=engine.decide(keys("hello"));check(result.text==L"hello","dictionary decision changed during refresh");timings.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
        std::shared_ptr<const BankSnapshot> snapshot;
        for(int i=0;i<1500;++i){snapshot=loader.latest();if(snapshot)break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        check(snapshot && snapshot->vocabulary.counts.at("en").size()==100000,"large snapshot incomplete");
        engine.snapshot_vocabulary=std::shared_ptr<const Vocabulary>(snapshot,&snapshot->vocabulary);
        check(engine.decide(keys("hello")).text==L"hello","large bank changed valid dictionary choice");engine.snapshot_vocabulary.reset();
        std::sort(timings.begin(),timings.end());std::cout<<"METRIC concurrent 100000-word refresh decision p95_ms="<<timings[timings.size()*95/100]<<'\n';loader.stop();
    });
    test("SQLite import counts and reversible batches",[&]{ Store db(temporary/L"words.db"); auto b=extract_text(L"hello hello السلام"); b.source=L"first.pdf"; auto first=db.add_import(b); b.source=L"second.pdf"; auto second=db.add_import(b); check(db.vocabulary().counts["en"][L"hello"]==4,"counts not aggregated"); check(db.undo_import(first),"undo failed"); check(db.vocabulary().counts["en"][L"hello"]==2,"undo lost shared words"); check(!db.undo_import(first),"undo not idempotent"); db.undo_import(second); check(db.vocabulary().counts.empty(),"undo left words"); });
    test("SQLite settings and preference persistence",[&]{ Store db(temporary/L"settings.db"); Settings s; s.bank_minimum=1; s.switch_minimum=4; s.mode=Mode::Automatic; s.locked=true; db.save_settings(s); db.prefer("test","ar"); Store reopened(temporary/L"settings.db",true); auto loaded=reopened.settings(); check(loaded.bank_minimum==1 && loaded.switch_minimum==4 && loaded.mode==Mode::Automatic && loaded.locked,"settings lost"); check(reopened.vocabulary().preferences["test"]=="ar","preference lost"); check(reopened.revision()==2,"revision did not advance"); });
    test("Database rejects invalid or short direct imports",[&]{ Store db(temporary/L"filter.db"); ImportBatch b; b.words["en"]={{L"cat",4},{L"hello",3},{L"hello123",1}}; db.add_import(b); auto words=db.vocabulary().counts["en"]; check(words.size()==1 && words[L"hello"]==3,"bank constraints bypassed"); });
    test("App exclusions are exact and case insensitive",[]{ check(app_excluded(L" cmd.exe ; Code.exe",L"CMD.EXE"),"case matching"); check(!app_excluded(L"cmd.exe",L"mycmd.exe"),"substring exclusion"); });
    test("Private banks do not overwrite another instance's tray status",[&]{
        struct Restore {std::wstring value;~Restore(){SetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",value.empty()?nullptr:value.c_str());}} restore;
        wchar_t old[32768]{};DWORD count=GetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",old,32768);if(count&&count<32768)restore.value.assign(old,count);
        auto firstBank=temporary/L"status-first",secondBank=temporary/L"status-second";
        SetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",firstBank.c_str());Status first;Engine signal;signal.locked=true;first.publish(signal);
        SetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",secondBank.c_str());Status second;signal.locked=false;second.publish(signal);
        check(first.read().locked==1 && second.read().locked==0,"private bank status leaked between instances");
    });
    test("Diagnostic consent expires and can be stopped",[&]{
        Environment bank(L"AUTOKEYBOARD_DATA_DIR",temporary.c_str()),trace(L"AUTOKEYBOARD_TRACE",nullptr);
        enable_diagnostics(temporary,true);auto config=diagnostic_config();check(config.active(),"diagnostic consent not loaded");
        check(!config.message_bridge.has_value(),"Notepad bridge override escaped into another executable");
        std::filesystem::remove(temporary/L"diagnostics.enabled");check(!config.active(),"stopped trace remained active");
        std::ofstream(temporary/L"diagnostics.enabled")<<diagnostic_time()-1<<" 0\n";
        check(!diagnostic_config().active(),"expired consent accepted");
        std::filesystem::remove(temporary/L"diagnostics.enabled");
    });
    test("IME COM factory and unload lifecycle",[&]{
        Environment bank(L"AUTOKEYBOARD_DATA_DIR",temporary.c_str()),trace(L"AUTOKEYBOARD_TRACE",nullptr);
        enable_diagnostics(temporary,false);
        auto dll=LoadLibraryW((module_folder()/L"AutoKeyboardIME.dll").c_str()); check(dll!=nullptr,"DLL cannot load");
        auto factoryFn=reinterpret_cast<HRESULT(__stdcall*)(REFCLSID,REFIID,void**)>(GetProcAddress(dll,"DllGetClassObject"));
        auto unload=reinterpret_cast<HRESULT(__stdcall*)()>(GetProcAddress(dll,"DllCanUnloadNow"));
        check(factoryFn && unload,"missing exports"); check(unload()==S_OK,"initial object leak");
        IClassFactory* factory=nullptr; check(SUCCEEDED(factoryFn(ServiceId,IID_IClassFactory,reinterpret_cast<void**>(&factory))),"factory");
        ITfTextInputProcessorEx* service=nullptr; check(SUCCEEDED(factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(&service))),"service");
        check(unload()==S_FALSE,"live object unloadable"); ITfKeyEventSink* sink=nullptr;
        check(SUCCEEDED(service->QueryInterface(IID_ITfKeyEventSink,reinterpret_cast<void**>(&sink))),"sink missing"); sink->Release();
        check(service->ActivateEx(nullptr,0,0)==E_INVALIDARG,"null manager accepted"); service->Release(); factory->Release();
        check(unload()==S_OK,"COM objects leaked"); FreeLibrary(dll);
        std::ifstream log(temporary/L"notepad-diagnostic.log");std::string content((std::istreambuf_iterator<char>(log)),{});
        check(content.find("activate hr=")!=std::string::npos && content.find("version=0.2.0")!=std::string::npos,"DLL did not read process-independent diagnostic consent");
        check(content.find(sizeof(void*)==8?"bits=64":"bits=32")!=std::string::npos,"diagnostic architecture was not decimal");
        std::filesystem::remove(temporary/L"diagnostics.enabled");
    });
    test("Bundled 50k frequency corpus and protected words",[&]{
        check(!engine.packs[0].frequency.empty() && !engine.packs[1].frequency.empty(),"50k frequency dictionaries not loaded");
        check(engine.packs[0].frequency_rank(L"hello") < 20000,"common English word missing from frequency list");
        check(engine.packs[1].frequency_rank(L"السلام") < 20000,"common Arabic word missing from frequency list");
        auto protFile = temporary / L"protected-words.txt";
        { std::ofstream out(protFile, std::ios::binary); out << "# comment\r\nhgsghl\r\n"; }
        engine.load_protected_words(protFile);
        engine.current = "en";
        auto d = engine.decide(keys("hgsghl"));
        check(!d.changed && d.reason == "protected","protected word was converted");
        engine.protected_words.clear();
    });
    test("GlobalHook automatic replacement, layout switching, and lam-alef length",[&]{
        Settings settings{};
        std::string activeLang = "en";
        size_t lastBackspaces = 0;
        std::wstring lastReplaced;
        std::string switchedTo;
        HookCallbacks cb{};
        cb.detect_language = [&](HWND){ return activeLang; };
        cb.target_allowed = [&](HWND, HWND){ return true; };
        cb.replace_text = [&](size_t bs, const std::wstring& rep, bool){ lastBackspaces = bs; lastReplaced = rep; };
        cb.switch_layout = [&](HWND, HWND, const std::string& lang){ switchedTo = lang; activeLang = lang; };
        GlobalHook hook(engine, settings, cb);
        HWND fg = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1001));
        DWORD tick = 1000;
        auto tap = [&](DWORD vk) {
            KBDLLHOOKSTRUCT down{vk, 0, 0, tick += 20, 0};
            bool eaten = hook.process_key(WM_KEYDOWN, down, fg, fg);
            KBDLLHOOKSTRUCT up{vk, 0, LLKHF_UP, tick += 10, 0};
            hook.process_key(WM_KEYUP, up, fg, fg);
            return eaten;
        };
        for (char c : std::string("HGSGHL")) check(!tap(static_cast<DWORD>(c)), "letter should not be eaten");
        check(tap(VK_SPACE), "space should be eaten on conversion");
        check(lastBackspaces == 6 && lastReplaced == L"السلام " && switchedTo == "ar", "hgsghl did not convert and switch layout to ar");
        // Now activeLang is "ar"; typing English "book" presses B ('لا' = 2 chars) + O + O + K = 5 Arabic chars on screen!
        lastBackspaces = 0; lastReplaced.clear(); switchedTo.clear();
        for (char c : std::string("BOOK")) tap(static_cast<DWORD>(c));
        check(tap(VK_SPACE), "space should be eaten on book conversion");
        check(lastBackspaces == 5 && lastReplaced == L"book " && switchedTo == "en", "lam-alef 2-char backspace count or conversion failed");
    });
    test("GlobalHook Undo, Manual Convert, Lock, In-word Backspace, and Alt+Shift",[&]{
        Settings settings{};
        std::string activeLang = "en";
        size_t lastBackspaces = 0;
        std::wstring lastReplaced;
        std::string switchedTo;
        bool lockNotified = false;
        HookCallbacks cb{};
        cb.detect_language = [&](HWND){ return activeLang; };
        cb.target_allowed = [&](HWND, HWND){ return true; };
        cb.replace_text = [&](size_t bs, const std::wstring& rep, bool){ lastBackspaces = bs; lastReplaced = rep; };
        cb.switch_layout = [&](HWND, HWND, const std::string& lang){ switchedTo = lang; activeLang = lang; };
        cb.lock_changed = [&](bool locked){ lockNotified = locked; };
        GlobalHook hook(engine, settings, cb);
        HWND fg = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x2002));
        DWORD tick = 2000;
        auto key_down = [&](DWORD vk){ KBDLLHOOKSTRUCT k{vk, 0, 0, tick += 20, 0}; return hook.process_key(WM_KEYDOWN, k, fg, fg); };
        auto key_up = [&](DWORD vk){ KBDLLHOOKSTRUCT k{vk, 0, LLKHF_UP, tick += 10, 0}; return hook.process_key(WM_KEYUP, k, fg, fg); };
        auto tap = [&](DWORD vk){ bool e = key_down(vk); key_up(vk); return e; };

        // 1. In-word backspace fixes typo ("hgsghxl" -> Backspace -> "l" -> "السلام ")
        for (char c : std::string("HGSGHX")) tap(static_cast<DWORD>(c));
        tap(VK_BACK);
        tap('L');
        check(tap(VK_SPACE) && lastReplaced == L"السلام " && activeLang == "ar", "in-word backspace prevented conversion");

        // 2. Undo (Ctrl+Alt+Backspace) restores original text and layout
        key_down(VK_LCONTROL); key_down(VK_LMENU);
        check(key_down(VK_BACK), "Ctrl+Alt+Backspace should be eaten on undo");
        key_up(VK_BACK); key_up(VK_LMENU); key_up(VK_LCONTROL);
        check(lastBackspaces == 7 && lastReplaced == L"hgsghl " && activeLang == "en", "Undo did not restore original word and layout");

        // 3. Manual convert (Ctrl+Alt+Space) converts short word ("cat") even below switch_minimum
        activeLang = "ar";
        for (char c : std::string("CAT")) tap(static_cast<DWORD>(c));
        key_down(VK_LCONTROL); key_down(VK_LMENU);
        check(key_down(VK_SPACE), "Ctrl+Alt+Space should manually convert");
        key_up(VK_SPACE); key_up(VK_LMENU); key_up(VK_LCONTROL);
        check(lastReplaced == L"cat" && activeLang == "en", "Ctrl+Alt+Space manual conversion failed");

        // 4. Alt+Shift direct EN/AR toggle switches layout and protects next word
        key_down(VK_LMENU); key_down(VK_LSHIFT);
        key_up(VK_LSHIFT); key_up(VK_LMENU);
        check(activeLang == "ar" && hook.is_manual_word(), "Alt+Shift did not toggle layout and mark manual word");

        // 5. Ctrl+Alt+L toggles lock
        key_down(VK_LCONTROL); key_down(VK_LMENU);
        check(key_down('L') && settings.locked && lockNotified, "Ctrl+Alt+L did not lock");
        key_up('L');
        check(key_down('L') && !settings.locked && !engine.locked, "Ctrl+Alt+L did not unlock");
        key_up('L'); key_up(VK_LMENU); key_up(VK_LCONTROL);

        // 6. Backspace on empty buffer does NOT poison subsequent words
        activeLang = "en"; lastBackspaces = 0; lastReplaced.clear();
        tap(VK_BACK); tap(VK_BACK);
        for (char c : std::string("HGSGHL")) tap(static_cast<DWORD>(c));
        check(tap(VK_SPACE) && lastReplaced == L"السلام " && activeLang == "ar", "backspace on empty buffer poisoned next word");

        // 7. Arrow keys and Delete do NOT poison subsequent words
        activeLang = "en"; lastBackspaces = 0; lastReplaced.clear();
        tap(VK_LEFT); tap(VK_RIGHT); tap(VK_DOWN); tap(VK_DELETE);
        for (char c : std::string("HGSGHL")) tap(static_cast<DWORD>(c));
        check(tap(VK_SPACE) && lastReplaced == L"السلام " && activeLang == "ar", "arrow keys poisoned next word");

        // 8. 5-second typing pause does NOT poison subsequent words
        activeLang = "en"; lastBackspaces = 0; lastReplaced.clear();
        tap('H');
        tick += 6000; // simulate >5s pause
        for (char c : std::string("HGSGHL")) tap(static_cast<DWORD>(c));
        check(tap(VK_SPACE) && lastReplaced == L"السلام " && activeLang == "ar", "idle timeout poisoned next word");

        // 9. Multi-character trailing punctuation (e.g. "!!") converts core word and preserves punctuation
        activeLang = "en"; lastBackspaces = 0; lastReplaced.clear();
        for (char c : std::string("HGSGHL")) tap(static_cast<DWORD>(c));
        key_down(VK_LSHIFT);
        key_down(0x31); key_up(0x31); // '!'
        key_down(0x31); key_up(0x31); // '!'
        key_up(VK_LSHIFT);
        check(tap(VK_SPACE) && lastReplaced.find(L"السلام") != std::wstring::npos && activeLang == "ar", "multi-exclamation trailing punctuation failed to convert");

        // 10. Single Backspace after invalid token ("mp4") does NOT corrupt remaining prefix ("mp")
        activeLang = "en"; lastBackspaces = 0; lastReplaced.clear();
        tap('M'); tap('P'); tap('4');
        tap(VK_BACK); // deletes '4', leaving "mp" before caret
        for (char c : std::string("HGSGHL")) tap(static_cast<DWORD>(c));
        check(!tap(VK_SPACE) && lastReplaced.empty(), "single backspace after mp4 corrupted mp prefix");

        // 11. Mid-word Left Arrow ("hel" + Left + "hgsghl") does NOT convert inside existing word
        activeLang = "en"; lastBackspaces = 0; lastReplaced.clear();
        for (char c : std::string("HEL")) tap(static_cast<DWORD>(c));
        tap(VK_LEFT); // caret is now inside "he|l"
        for (char c : std::string("HGSGHL")) tap(static_cast<DWORD>(c));
        check(!tap(VK_SPACE) && lastReplaced.empty(), "mid-word VK_LEFT converted inside existing word");
        // Subsequent word after Space boundary converts normally
        for (char c : std::string("HGSGHL")) tap(static_cast<DWORD>(c));
        check(tap(VK_SPACE) && lastReplaced == L"السلام " && activeLang == "ar", "word after mid-word edit boundary did not convert");
    });
    std::cout << passed << " passed, " << failed << " failed\n";
    // Only files created by this test process are removed.
    if(temporary.parent_path()==std::filesystem::temp_directory_path())std::filesystem::remove_all(temporary);
    CoUninitialize();
    return failed ? 1 : 0;
}
