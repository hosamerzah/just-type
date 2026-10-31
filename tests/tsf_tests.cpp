#include "tsf.hpp"
#include "profile.hpp"
#include <textstor.h>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <inputscope.h>
#include <chrono>

using namespace ak;
namespace {
bool installedProfile=false;
int soakSeconds=0;
class Blocked : public std::runtime_error { public:using std::runtime_error::runtime_error; };
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void ok(HRESULT hr, const char* message) { if (FAILED(hr)) { std::cerr << "HRESULT=" << std::hex << static_cast<unsigned long>(hr) << std::dec << '\n'; throw std::runtime_error(message); } }
class TextStore final : public ITextStoreACP, public ITfContextOwnerCompositionSink {
    ULONG references_ = 1;
    ComPtr<ITextStoreACPSink> sink_;
    bool locked_ = false;
public:
    std::wstring text;
    TS_SELECTION_ACP selection{0,0,{TS_AE_NONE,FALSE}};
    bool readonly = false;
    bool failText=false,failSelection=false;
    bool deferLocks=false;
    DWORD pendingLock=0;
    int compositions=0;
    HWND window = nullptr;
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if(iid==IID_IUnknown || iid==IID_ITextStoreACP)*out=static_cast<ITextStoreACP*>(this);
        else if(iid==IID_ITfContextOwnerCompositionSink)*out=static_cast<ITfContextOwnerCompositionSink*>(this);
        if(!*out)return E_NOINTERFACE; AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override { auto n=--references_; if(!n)delete this; return n; }
    STDMETHODIMP AdviseSink(REFIID iid,IUnknown* unknown,DWORD) override { return iid==IID_ITextStoreACPSink ? unknown->QueryInterface(iid,reinterpret_cast<void**>(sink_.put())) : E_NOINTERFACE; }
    STDMETHODIMP UnadviseSink(IUnknown*) override { sink_.reset(); return S_OK; }
    STDMETHODIMP RequestLock(DWORD flags,HRESULT* session) override {
        if(!session)return E_POINTER;
        if(locked_){*session=TS_E_SYNCHRONOUS; return S_OK;}
        if(deferLocks){if(flags&TS_LF_SYNC)*session=TS_E_SYNCHRONOUS;else{pendingLock=flags;*session=TS_S_ASYNC;}return S_OK;}
        if(!sink_){*session=E_FAIL; return E_FAIL;}
        locked_=true; *session=sink_->OnLockGranted(flags); locked_=false; return S_OK;
    }
    STDMETHODIMP GetStatus(TS_STATUS* status) override { *status={readonly ? static_cast<DWORD>(TS_SD_READONLY) : 0u,TS_SS_NOHIDDENTEXT}; return S_OK; }
    STDMETHODIMP QueryInsert(LONG start,LONG end,ULONG,LONG* outStart,LONG* outEnd) override { if(start<0 || end<start || end>static_cast<LONG>(text.size()))return E_INVALIDARG; *outStart=start; *outEnd=end; return S_OK; }
    STDMETHODIMP GetSelection(ULONG index,ULONG count,TS_SELECTION_ACP* out,ULONG* fetched) override { *fetched=0; if(count && (index==0 || index==TS_DEFAULT_SELECTION)){*out=selection; *fetched=1;} return S_OK; }
    STDMETHODIMP SetSelection(ULONG count,const TS_SELECTION_ACP* in) override { if(failSelection)return E_FAIL;if(count!=1)return E_INVALIDARG; selection=*in; return S_OK; }
    STDMETHODIMP GetText(LONG start,LONG end,WCHAR* plain,ULONG requested,ULONG* returned,TS_RUNINFO* runs,ULONG runCount,ULONG* runsReturned,LONG* next) override {
        if(end==-1)end=static_cast<LONG>(text.size());
        if(start<0 || end<start || end>static_cast<LONG>(text.size()))return E_INVALIDARG;
        ULONG count=static_cast<ULONG>(end-start);
        if(plain && requested) { count=std::min(count,requested); std::copy_n(text.data()+start,count,plain); }
        if(returned)*returned=plain && requested ? count : 0;
        if(runsReturned)*runsReturned=runCount && count ? 1 : 0;
        if(runs && runCount && count)runs[0]={count,TS_RT_PLAIN};
        if(next)*next=start+count;
        return S_OK;
    }
    STDMETHODIMP SetText(DWORD,LONG start,LONG end,const WCHAR* in,ULONG count,TS_TEXTCHANGE* change) override {
        if(failText)return E_FAIL;
        if(start<0 || end<start || end>static_cast<LONG>(text.size()))return E_INVALIDARG;
        if(change)*change={start,end,start+static_cast<LONG>(count)};
        text.replace(static_cast<size_t>(start),static_cast<size_t>(end-start),in,count);
        selection.acpStart=selection.acpEnd=start+static_cast<LONG>(count); return S_OK;
    }
    STDMETHODIMP InsertTextAtSelection(DWORD flags,const WCHAR* in,ULONG count,LONG* start,LONG* end,TS_TEXTCHANGE* change) override {
        if(start)*start=selection.acpStart;
        if(end)*end=selection.acpStart+static_cast<LONG>(count);
        if(flags&TS_IAS_QUERYONLY)return S_OK;
        return SetText(0,selection.acpStart,selection.acpEnd,in,count,change);
    }
    STDMETHODIMP GetFormattedText(LONG,LONG,IDataObject**) override { return E_NOTIMPL; }
    STDMETHODIMP GetEmbedded(LONG,REFGUID,REFIID,IUnknown**) override { return TS_E_NOOBJECT; }
    STDMETHODIMP QueryInsertEmbedded(const GUID*,const FORMATETC*,BOOL* out) override { *out=FALSE; return S_OK; }
    STDMETHODIMP InsertEmbedded(DWORD,LONG,LONG,IDataObject*,TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD,IDataObject*,LONG*,LONG*,TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP RequestSupportedAttrs(DWORD,ULONG,const TS_ATTRID*) override { return S_OK; }
    STDMETHODIMP RequestAttrsAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD) override { return S_OK; }
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD) override { return S_OK; }
    STDMETHODIMP FindNextAttrTransition(LONG,LONG halt,ULONG,const TS_ATTRID*,DWORD,LONG* next,BOOL* found,LONG* offset) override { *next=halt; *found=FALSE; *offset=0; return S_OK; }
    STDMETHODIMP RetrieveRequestedAttrs(ULONG,TS_ATTRVAL*,ULONG* count) override { *count=0; return S_OK; }
    STDMETHODIMP GetEndACP(LONG* end) override { *end=static_cast<LONG>(text.size()); return S_OK; }
    STDMETHODIMP GetActiveView(TsViewCookie* view) override { *view=1; return S_OK; }
    STDMETHODIMP GetACPFromPoint(TsViewCookie,const POINT*,DWORD,LONG* acp) override { *acp=0; return S_OK; }
    STDMETHODIMP GetTextExt(TsViewCookie,LONG,LONG,RECT* rect,BOOL* clipped) override { *rect={0,0,200,24}; *clipped=FALSE; return S_OK; }
    STDMETHODIMP GetScreenExt(TsViewCookie,RECT* rect) override { *rect={0,0,800,600}; return S_OK; }
    STDMETHODIMP GetWnd(TsViewCookie,HWND* out) override { *out=window; return S_OK; }
    STDMETHODIMP OnStartComposition(ITfCompositionView*,BOOL* accept) override { *accept=TRUE;++compositions; return S_OK; }
    STDMETHODIMP OnUpdateComposition(ITfCompositionView*,ITfRange*) override { return S_OK; }
    STDMETHODIMP OnEndComposition(ITfCompositionView*) override { --compositions;return S_OK; }
    void grant_pending() {deferLocks=false;if(pendingLock){DWORD flags=pendingLock;pendingLock=0;locked_=true;sink_->OnLockGranted(flags);locked_=false;}}
    void native(std::wstring_view value) {
        TS_TEXTCHANGE change{}; SetText(0,selection.acpStart,selection.acpEnd,value.data(),static_cast<ULONG>(value.size()),&change);
        if(sink_){sink_->OnTextChange(0,&change); sink_->OnSelectionChange();}
    }
    void move_caret(LONG position) { selection.acpStart=selection.acpEnd=position; if(sink_)sink_->OnSelectionChange(); }
    void clear_text(){TS_TEXTCHANGE change{};SetText(0,0,static_cast<LONG>(text.size()),L"",0,&change);if(sink_){sink_->OnTextChange(0,&change);sink_->OnSelectionChange();}}
};
// Windows only accepts AdviseKeyEventSink while TSF itself is activating a
// registered TIP. The harness invokes callbacks directly, so intercept only
// that registration step; document contexts, edit locks and ranges remain real
// Windows TSF objects. This does not test OS profile selection/key dispatch.
class TestManager final : public ITfThreadMgr, public ITfKeystrokeMgr {
    ULONG refs_=1;
    ComPtr<ITfThreadMgr> real_;
    ComPtr<ITfKeyEventSink> sink_;
    ComPtr<ITfContext> focused_context() {ComPtr<ITfDocumentMgr> doc;ComPtr<ITfContext> ctx;if(SUCCEEDED(real_->GetFocus(doc.put()))&&doc)doc->GetTop(ctx.put());return ctx;}
public:
    explicit TestManager(ITfThreadMgr* real):real_(real){}
    int bridgeCalls=0;
    CLSID foreground=ServiceId;
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==IID_IUnknown || iid==IID_ITfThreadMgr)*out=static_cast<ITfThreadMgr*>(this);
        else if(iid==IID_ITfKeystrokeMgr)*out=static_cast<ITfKeystrokeMgr*>(this);
        else return real_->QueryInterface(iid,out);
        AddRef();return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override{return ++refs_;}
    STDMETHODIMP_(ULONG) Release() override{auto n=--refs_;if(!n)delete this;return n;}
    STDMETHODIMP Activate(TfClientId* id) override{return real_->Activate(id);}
    STDMETHODIMP Deactivate() override{return real_->Deactivate();}
    STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr** out) override{return real_->CreateDocumentMgr(out);}
    STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs** out) override{return real_->EnumDocumentMgrs(out);}
    STDMETHODIMP GetFocus(ITfDocumentMgr** out) override{return real_->GetFocus(out);}
    STDMETHODIMP SetFocus(ITfDocumentMgr* in) override{return real_->SetFocus(in);}
    STDMETHODIMP AssociateFocus(HWND w,ITfDocumentMgr* in,ITfDocumentMgr** out) override{return real_->AssociateFocus(w,in,out);}
    STDMETHODIMP IsThreadFocus(BOOL* out) override{return real_->IsThreadFocus(out);}
    STDMETHODIMP GetFunctionProvider(REFCLSID id,ITfFunctionProvider** out) override{return real_->GetFunctionProvider(id,out);}
    STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders** out) override{return real_->EnumFunctionProviders(out);}
    STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr** out) override{return real_->GetGlobalCompartment(out);}
    STDMETHODIMP AdviseKeyEventSink(TfClientId,ITfKeyEventSink* sink,BOOL) override{sink_=ComPtr<ITfKeyEventSink>(sink);return S_OK;}
    STDMETHODIMP UnadviseKeyEventSink(TfClientId) override{sink_.reset();return S_OK;}
    STDMETHODIMP GetForeground(CLSID* id) override{*id=sink_?foreground:GUID_NULL;return sink_?S_OK:S_FALSE;}
    STDMETHODIMP TestKeyDown(WPARAM key,LPARAM flags,BOOL* eaten) override{auto ctx=focused_context();return sink_?sink_->OnTestKeyDown(ctx.get(),key,flags,eaten):S_FALSE;}
    STDMETHODIMP TestKeyUp(WPARAM key,LPARAM flags,BOOL* eaten) override{auto ctx=focused_context();return sink_?sink_->OnTestKeyUp(ctx.get(),key,flags,eaten):S_FALSE;}
    STDMETHODIMP KeyDown(WPARAM key,LPARAM flags,BOOL* eaten) override{++bridgeCalls;auto ctx=focused_context();return sink_?sink_->OnKeyDown(ctx.get(),key,flags,eaten):S_FALSE;}
    STDMETHODIMP KeyUp(WPARAM key,LPARAM flags,BOOL* eaten) override{auto ctx=focused_context();return sink_?sink_->OnKeyUp(ctx.get(),key,flags,eaten):S_FALSE;}
    STDMETHODIMP GetPreservedKey(ITfContext*,const TF_PRESERVEDKEY*,GUID*) override{return E_NOTIMPL;}
    STDMETHODIMP IsPreservedKey(REFGUID,const TF_PRESERVEDKEY*,BOOL*) override{return E_NOTIMPL;}
    STDMETHODIMP PreserveKey(TfClientId,REFGUID,const TF_PRESERVEDKEY*,const WCHAR*,ULONG) override{return E_NOTIMPL;}
    STDMETHODIMP UnpreserveKey(REFGUID,const TF_PRESERVEDKEY*) override{return E_NOTIMPL;}
    STDMETHODIMP SetPreservedKeyDescription(REFGUID,const WCHAR*,ULONG) override{return E_NOTIMPL;}
    STDMETHODIMP GetPreservedKeyDescription(REFGUID,BSTR*) override{return E_NOTIMPL;}
    STDMETHODIMP SimulatePreservedKey(ITfContext*,REFGUID,BOOL*) override{return E_NOTIMPL;}
};
class Harness {
    HMODULE dll_ = nullptr;
    ComPtr<ITfThreadMgr> manager_;
    ComPtr<ITfDocumentMgr> document_;
    ComPtr<ITfContext> context_;
    ComPtr<ITfTextInputProcessorEx> service_;
    ComPtr<ITfKeyEventSink> keys_;
    ComPtr<ITfKeystrokeMgr> dispatch_;
    ComPtr<TestManager> adapter_;
    ComPtr<TextStore> store_;
    BYTE oldKeyboard_[256]{};
    TfClientId ownerClient_ = 0;
public:
    explicit Harness(DWORD activationFlags=0) {
        try {
        GetKeyboardState(oldKeyboard_);
        ok(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_ITfThreadMgr,reinterpret_cast<void**>(manager_.put())),"thread manager");
        TfClientId client=0; ok(manager_->Activate(&client),"activate TSF");
        ownerClient_=client;
        ok(manager_->CreateDocumentMgr(document_.put()),"document manager");
        auto rawStore=new TextStore; store_=ComPtr<TextStore>(rawStore); rawStore->Release();
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"AutoKeyboard.OwnedTsfTest";
        RegisterClassW(&cls);
        store_->window=CreateWindowW(cls.lpszClassName,L"AutoKeyboard test",WS_OVERLAPPED,0,0,100,50,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        TfEditCookie cookie=0;
        ok(document_->CreateContext(client,0,static_cast<ITextStoreACP*>(store_.get()),context_.put(),&cookie),"create TSF context");
        ok(document_->Push(context_.get()),"push context");
        ComPtr<ITfDocumentMgr> previousDocument;
        ok(manager_->AssociateFocus(store_->window,document_.get(),previousDocument.put()),"associate test text store");
        ok(manager_->SetFocus(document_.get()),"focus test document");
        if(installedProfile) {
            ShowWindow(store_->window,SW_SHOW);SetForegroundWindow(store_->window);
            ::SetFocus(store_->window);
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            BOOL focused=FALSE;manager_->IsThreadFocus(&focused);
            std::cout<<"TSF test thread focus="<<focused<<'\n';
            if(!focused || ::GetFocus()!=store_->window)throw Blocked("Windows did not grant keyboard focus to the visible test host");
        }
        if(installedProfile) {
            ok(activate_keyboard(false),"activate Windows-registered profile");
            ok(manager_->QueryInterface(IID_ITfKeystrokeMgr,reinterpret_cast<void**>(dispatch_.put())),"real Windows key dispatcher");
            CLSID foreground{};
            HRESULT foregroundResult=dispatch_->GetForeground(&foreground);
            wchar_t identity[40]{};StringFromGUID2(foreground,identity,40);
            std::cout<<"Windows foreground key sink: "<<utf8(identity)<<" hr="<<std::hex<<foregroundResult<<std::dec<<'\n';
            require(SUCCEEDED(foregroundResult) && IsEqualCLSID(foreground,ServiceId),"Installed profile did not attach its Windows key sink");
            return;
        }
        dll_=LoadLibraryW((module_folder()/L"AutoKeyboardIME.dll").c_str()); require(dll_!=nullptr,"load DLL");
        auto factoryFn=reinterpret_cast<HRESULT(__stdcall*)(REFCLSID,REFIID,void**)>(GetProcAddress(dll_,"DllGetClassObject"));
        ComPtr<IClassFactory> factory;
        ok(factoryFn(ServiceId,IID_IClassFactory,reinterpret_cast<void**>(factory.put())),"factory");
        ok(factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(service_.put())),"service");
        ComPtr<ITfClientId> ids;
        ok(manager_->QueryInterface(IID_ITfClientId,reinterpret_cast<void**>(ids.put())),"client IDs");
        TfClientId serviceClient=0; ok(ids->GetClientId(ServiceId,&serviceClient),"service client ID");
        auto wrapper=new TestManager(manager_.get());
        adapter_=ComPtr<TestManager>(wrapper);
        HRESULT activation=service_->ActivateEx(wrapper,serviceClient,activationFlags); wrapper->Release();
        ok(activation,"activate IME");
        ok(service_->QueryInterface(IID_ITfKeyEventSink,reinterpret_cast<void**>(keys_.put())),"key sink");
        // Settle OS activation/focus notifications before posting test keys.
        // These can be queued while the installed profile loads dictionaries.
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        }catch(...){cleanup();throw;}
    }
    ~Harness(){cleanup();}
    void cleanup() {
        if(store_ && store_->window){HMODULE module=LoadLibraryExW(L"msctf.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(module){auto set=reinterpret_cast<HRESULT(WINAPI*)(HWND,InputScope)>(GetProcAddress(module,"SetInputScope"));if(set)set(store_->window,IS_DEFAULT);FreeLibrary(module);}}
        if(service_)service_->Deactivate(); keys_.reset(); service_.reset(); dispatch_.reset();
        if(document_)document_->Pop(TF_POPF_ALL);
        context_.reset(); document_.reset();
        if(manager_)manager_->Deactivate(); manager_.reset();
        if(store_ && store_->window)DestroyWindow(store_->window); store_.reset();
        if(dll_)FreeLibrary(dll_);
        SetKeyboardState(oldKeyboard_);
    }
    bool key(unsigned key,bool control=false,bool alt=false,bool shift=false,unsigned repeats=1) {
        BYTE state[256]{}; state[VK_CONTROL]=control?0x80:0; state[VK_MENU]=alt?0x80:0; state[VK_SHIFT]=shift?0x80:0; SetKeyboardState(state);
        BOOL interested=FALSE,consumed=FALSE;
        LPARAM downFlags=repeats|(static_cast<LPARAM>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC))<<16);
        if(dispatch_) {
            ok(dispatch_->TestKeyDown(key,downFlags,&interested),"Windows test keydown");
            if(interested)ok(dispatch_->KeyDown(key,downFlags,&consumed),"Windows keydown");
        } else {
            ok(keys_->OnTestKeyDown(context_.get(),key,downFlags,&interested),"test keydown");
            if(interested)ok(keys_->OnKeyDown(context_.get(),key,downFlags,&consumed),"keydown");
        }
        BOOL keyUp=FALSE;
        LPARAM upFlags=(downFlags&~static_cast<LPARAM>(0xffff))|static_cast<LPARAM>(0xc0000001u);
        if(dispatch_){BOOL wanted=FALSE;ok(dispatch_->TestKeyUp(key,upFlags,&wanted),"Windows test keyup");if(wanted)ok(dispatch_->KeyUp(key,upFlags,&keyUp),"Windows keyup");}else keys_->OnKeyUp(context_.get(),key,upFlags,&keyUp);
        require((keyUp!=FALSE)==(consumed!=FALSE),"keydown/up consumption mismatched");
        if(!consumed && !control && !alt){ if(key==VK_SPACE)store_->native(std::wstring(repeats,L' ')); else if(key==VK_RETURN)store_->native(std::wstring(repeats,L'\n'));else if(key==VK_BACK){store_->selection.acpStart=std::max<LONG>(0,store_->selection.acpStart-static_cast<LONG>(repeats));store_->native(L"");} }
        return consumed!=FALSE;
    }
    void type(const std::string& text) { for(char c:text)require(key(static_cast<unsigned>(c-'a'+'A')),"letter was not consumed"); }
    const std::wstring& text() const { return store_->text; }
    void focus_out() { keys_->OnSetFocus(FALSE); }
    void defer_locks(bool value){store_->deferLocks=value;}
    void grant_pending(){store_->grant_pending();}
    int compositions()const{return store_->compositions;}
    void clear_text(){keys_->OnSetFocus(FALSE);store_->clear_text();keys_->OnSetFocus(TRUE);}
    int bridge_calls()const{return adapter_?adapter_->bridgeCalls:0;}
    void other_profile(){adapter_->foreground=GUID_NULL;}
    void queued_word(const std::string& word,bool peek=false,bool hostForwarding=false) {
        ::SetFocus(store_->window);
        MSG settling{};while(PeekMessageW(&settling,nullptr,0,0,PM_REMOVE)){TranslateMessage(&settling);DispatchMessageW(&settling);}
        ok(manager_->SetFocus(document_.get()),"focus owned queued-input document");
        BYTE state[256]{};SetKeyboardState(state);
        for(char letter:word) {
            unsigned key=letter==' '?VK_SPACE:letter=='\n'?VK_RETURN:static_cast<unsigned>(letter-'a'+'A');
            LPARAM flags=1|(MapVirtualKeyW(key,MAPVK_VK_TO_VSC)<<16);
            PostMessageW(store_->window,WM_KEYDOWN,key,flags);
            PostMessageW(store_->window,WM_KEYUP,key,flags|0xc0000000u);
        }
        MSG message{};
        if(peek)PeekMessageW(&message,store_->window,WM_KEYDOWN,WM_KEYDOWN,PM_NOREMOVE);
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(hostForwarding && message.message==WM_KEYDOWN){BOOL want=FALSE,eaten=FALSE;keys_->OnTestKeyDown(context_.get(),message.wParam,message.lParam,&want);if(want)keys_->OnKeyDown(context_.get(),message.wParam,message.lParam,&eaten);if(eaten)message.message=WM_NULL;}
            if(message.message==WM_KEYDOWN && message.wParam==VK_RETURN)store_->native(L"\n");
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        if(word=="hgsghl " && text()!=L"السلام "){
            std::cout<<"Queued test diagnostics: bridge calls="<<bridge_calls()<<"; UTF-16=";
            for(wchar_t ch:text())std::cout<<std::hex<<static_cast<unsigned>(ch)<<' ';
            std::cout<<std::dec<<'\n';
        }
    }
    void unrelated_events() {
        ComPtr<ITfThreadMgrEventSink> events;ok(service_->QueryInterface(IID_ITfThreadMgrEventSink,reinterpret_cast<void**>(events.put())),"thread event sink");
        ComPtr<ITfDocumentMgr> other;ok(manager_->CreateDocumentMgr(other.put()),"other document");
        events->OnUninitDocumentMgr(other.get());
        events->OnSetFocus(document_.get(),document_.get());
        events->OnPushContext(context_.get());
    }
    void terminate_compositions() {
        ComPtr<ITfContextOwnerCompositionServices> services;
        if(SUCCEEDED(context_->QueryInterface(IID_ITfContextOwnerCompositionServices,reinterpret_cast<void**>(services.put()))))services->TerminateComposition(nullptr);
    }
    void move(LONG position) { store_->move_caret(position); }
    void readonly(bool value) { store_->readonly=value; }
    void fail_text(bool value){store_->failText=value;}
    void fail_selection(bool value){store_->failSelection=value;}
    void input_scope(InputScope scope){
        HMODULE module=LoadLibraryExW(L"msctf.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);require(module!=nullptr,"load input scope API");
        auto set=reinterpret_cast<HRESULT(WINAPI*)(HWND,InputScope)>(GetProcAddress(module,"SetInputScope"));
        HRESULT result=set?set(store_->window,scope):E_NOTIMPL;FreeLibrary(module);ok(result,"set actual window input scope");
    }
    LONG open_state() {
        ComPtr<ITfCompartmentMgr> manager;ok(manager_->QueryInterface(IID_ITfCompartmentMgr,reinterpret_cast<void**>(manager.put())),"thread compartments");
        ComPtr<ITfCompartment> compartment;ok(manager->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,compartment.put()),"open compartment");
        VARIANT value;VariantInit(&value);ok(compartment->GetValue(&value),"open state");
        LONG result=value.vt==VT_I4?value.lVal:-1;VariantClear(&value);return result;
    }
    void close_keyboard() {
        ComPtr<ITfCompartmentMgr> manager;ok(manager_->QueryInterface(IID_ITfCompartmentMgr,reinterpret_cast<void**>(manager.put())),"thread compartments");
        ComPtr<ITfCompartment> compartment;ok(manager->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,compartment.put()),"open compartment");
        VARIANT value;VariantInit(&value);value.vt=VT_I4;value.lVal=0;ok(compartment->SetValue(ownerClient_,&value),"close keyboard");
    }
    void disable() {
        ComPtr<ITfCompartmentMgr> compartments; ok(context_->QueryInterface(IID_ITfCompartmentMgr,reinterpret_cast<void**>(compartments.put())),"compartments");
        ComPtr<ITfCompartment> disabled; ok(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED,disabled.put()),"disabled compartment");
        VARIANT v; VariantInit(&v); v.vt=VT_I4; v.lVal=1; ok(disabled->SetValue(ownerClient_,&v),"disable context");
    }
};
}
int main(int argc,char** argv) {
    std::cout<<std::unitbuf;
    installedProfile=argc>1 && std::string(argv[1])=="--installed-profile";
    std::string filter=argc>2&&std::string(argv[1])=="--case"?argv[2]:"";
    if(argc>2 && std::string(argv[1])=="--soak-seconds"){
        try{soakSeconds=std::stoi(argv[2]);}catch(...){return 2;}
        if(soakSeconds<1 || soakSeconds>1800)return 2;filter="repeated mixed";
    }
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(installedProfile){auto state=desktop_preflight();std::cout<<utf8(state.report);if(!state.ready){CoUninitialize();return 3;}}
    else SetEnvironmentVariableW(L"AUTOKEYBOARD_MESSAGE_BRIDGE",L"1");
    auto temp=std::filesystem::temp_directory_path()/(L"AutoKeyboard-tsf-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(temp); SetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",temp.c_str());
    {Store db(temp/L"words.db");db.save_settings(Settings{});}
    int passed=0,failed=0,blocked=0;
    auto test=[&](const char* name,auto action){if(!filter.empty()&&std::string(name).find(filter)==std::string::npos)return;try{Harness host;action(host);++passed;std::cout<<"PASS "<<name<<'\n';}catch(const Blocked& e){++blocked;std::cout<<"BLOCKED "<<name<<": "<<e.what()<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    test("native composition converts word and preserves space",[](Harness& h){h.type("hgsghl");require(h.text()==L"hgsghl","provisional text");h.key(VK_SPACE);require(h.text()==L"السلام ","committed text or space wrong");});
    if(installedProfile){std::cout<<passed<<" passed, "<<failed<<" failed, "<<blocked<<" blocked\n";CoUninitialize();return failed?1:blocked?3:0;}
    test("profile activation opens Windows composition",[](Harness& h){require(h.open_state()==1,"keyboard stayed closed after activation");});
    test("restored OS open state does not disable the bilingual profile",[](Harness& h){h.close_keyboard();h.type("hgsghl");h.key(VK_SPACE);require(h.text()==L"السلام ","restored open state disabled conversion");});
    test("queued keys reach the service without host TSF forwarding",[](Harness& h){h.queued_word("hgsghl ");require(h.text()==L"السلام ","message bridge did not convert queued word");require(h.bridge_calls()==7,"bridge did not dispatch each key once");});
    test("peeking a key does not convert or duplicate it",[](Harness& h){h.queued_word("hgsghl ",true);require(h.text()==L"السلام ","peek duplicated a queued key");require(h.bridge_calls()==7,"peek dispatched a key twice");});
    test("queued keys in disabled fields pass through",[](Harness& h){h.disable();h.queued_word("hello ");require(h.text().empty(),"message bridge changed disabled field");require(h.bridge_calls()==0,"disabled keys sent to the bridge");});
    test("queued keys in read-only fields pass through",[](Harness& h){h.readonly(true);h.queued_word("hello ");require(h.text().empty()&&h.bridge_calls()==0,"read-only keys reached the bridge");});
    test("queued keys pass through with another keyboard profile",[](Harness& h){h.other_profile();h.queued_word("hello ");require(h.text().empty()&&h.bridge_calls()==0,"another keyboard profile was intercepted");});
    test("normal host TSF forwarding does not duplicate bridged keys",[](Harness& h){h.queued_word("hgsghl ",false,true);require(h.text()==L"السلام "&&h.bridge_calls()==7,"host forwarding duplicated text");});
    test("queued Enter commits before the native action once",[](Harness& h){h.queued_word("hgsghl\n",false,true);require(h.text()==L"السلام\n","queued Enter lost conversion or duplicated native action");});
    test("host terminating composition after each key still converts word on space",[](Harness& h){for(char c:std::string("hgsghl")){require(h.key(static_cast<unsigned>(c-'a'+'A')),"letter was not consumed");h.terminate_compositions();}h.key(VK_SPACE);require(h.text()==L"السلام ","host-terminated composition failed to convert word");});
    test("Arabic to English and mixed-language sentence",[](Harness& h){h.type("hgsghl");h.key(VK_SPACE);h.type("hello");h.key(VK_SPACE);require(h.text()==L"السلام hello ","mixed text changed or duplicated");});
    test("Backspace removes a physical key",[](Harness& h){h.type("hellp");h.key(VK_BACK);h.type("o");h.key(VK_SPACE);require(h.text()==L"hello ","backspace composition");});
    test("coalesced letter and space repeat counts are preserved",[](Harness& h){h.key('H',false,false,false,4);h.type("ello");h.key(VK_SPACE,false,false,false,3);require(h.text()==L"hhhhello   ","coalesced repeat lost or duplicated text");});
    test("coalesced Backspace commits before native deletion",[](Harness& h){h.type("hello");require(!h.key(VK_BACK,false,false,false,2),"coalesced native Backspace was swallowed");require(h.text()==L"hel","coalesced Backspace deleted wrong text");});
    test("oversized unknown word never converts a known suffix",[](Harness& h){std::string prefix(256,'x');h.type(prefix+"hgsghl");h.key(VK_SPACE);require(h.text()==std::wstring(256,L'x')+L"hgsghl ","long unknown word partially converted");h.type("hgsghl");h.key(VK_SPACE);require(h.text()==std::wstring(256,L'x')+L"hgsghl السلام ","typing failed after long word boundary");});
    test("Enter commits before native action once",[](Harness& h){h.type("hgsghl");require(!h.key(VK_RETURN),"Enter swallowed");require(h.text()==L"السلام\n","Enter committed twice or lost");});
    test("manual conversion after word boundary",[](Harness& h){h.type("hgsghl");h.key(VK_SPACE);require(h.key(VK_SPACE,true,true),"manual shortcut ignored");require(h.text()==L"hgsghl ","manual word range includes delimiter");});
    test("manual choice remains on next word boundary",[](Harness& h){h.type("hello");require(h.key(VK_SPACE,true,true),"manual in-progress conversion ignored");h.key(VK_SPACE);require(h.text()==L"اثممخ ","manual choice was automatically reversed");});
    test("undo manual choice while composing",[](Harness& h){h.type("hello");h.key(VK_SPACE,true,true);require(h.key(VK_BACK,true,true),"in-progress undo ignored");require(h.text()==L"hello","in-progress undo lost original");});
    test("undo manual choice after committing",[](Harness& h){h.type("hello");h.key(VK_SPACE,true,true);h.key(VK_SPACE);h.key(VK_BACK,true,true);require(h.text()==L"hello ","committed undo lost original");});
    test("modifier keys do not interrupt composition",[](Harness& h){h.type("hgsghl");require(!h.key(VK_CONTROL,true),"Ctrl consumed");h.key(VK_SPACE);require(h.text()==L"السلام ","Ctrl interrupted word");});
    test("language lock bypasses automatic decisions",[](Harness& h){h.key('L',true,true);h.type("hgsghl");h.key(VK_SPACE);require(h.text()==L"hgsghl ","locked conversion happened");});
    test("Arabic punctuation after a space uses current language",[](Harness& h){h.type("hgsghl");h.key(VK_SPACE);require(h.key(0xbf,false,false,true),"Arabic question mark not handled");require(h.text()==L"السلام ؟","wrong punctuation");});
    test("undo conversion restores original word",[](Harness& h){h.type("hgsghl");h.key(VK_SPACE);require(h.key(VK_BACK,true,true),"undo ignored");require(h.text()==L"hgsghl ","undo changed wrong range");});
    test("short words unchanged and manually convertible",[](Harness& h){h.type("cat");h.key(VK_SPACE);require(h.text()==L"cat ","short word switched");h.key(VK_SPACE,true,true);require(h.text()==L"ؤشف ","short manual conversion");});
    test("caret movement invalidates old-word conversion",[](Harness& h){h.type("hgsghl");h.key(VK_SPACE);h.move(0);require(!h.key(VK_BACK,true,true),"undo changed text after caret moved");require(h.text()==L"السلام ","stale range changed");});
    test("focus loss commits raw word without detection",[](Harness& h){h.type("hgsghl");h.focus_out();require(h.text()==L"hgsghl","focus changed text");});
    test("deferred focus cleanup ends the old composition",[](Harness& h){h.type("hgsghl");require(h.compositions()==1,"missing composition");h.defer_locks(true);h.focus_out();h.grant_pending();require(h.compositions()==0,"old composition survived focus loss");require(h.text()==L"hgsghl","focus cleanup rewrote text");h.type("hello");h.key(VK_SPACE);require(h.text()==L"hgsghlhello ","typing failed after deferred cleanup");});
    test("unrelated document events preserve the active word",[](Harness& h){h.type("hgsghl");h.unrelated_events();h.key(VK_SPACE);require(h.text()==L"السلام ","unrelated notification interrupted conversion");});
    test("read-only context passes keys through",[](Harness& h){h.readonly(true);require(!h.key('H'),"read-only intercepted");require(h.text().empty(),"read-only text modified");});
    test("disabled context passes keys through",[](Harness& h){h.disable();require(!h.key('H'),"disabled context intercepted");require(h.text().empty(),"disabled text changed");});
    test("actual password input scope passes keys through",[](Harness& h){h.input_scope(IS_PASSWORD);require(!h.key('H'),"password scope intercepted");require(h.text().empty(),"password scope changed text");});
    test("actual URL input scope passes keys through",[](Harness& h){h.input_scope(IS_URL);require(!h.key('H'),"URL scope intercepted");require(h.text().empty(),"URL scope changed text");});
    test("actual email input scope passes keys through",[](Harness& h){h.input_scope(IS_EMAIL_SMTPEMAILADDRESS);require(!h.key('H'),"email scope intercepted");require(h.text().empty(),"email scope changed text");});
    test("actual login input scope passes keys through",[](Harness& h){h.input_scope(IS_LOGINNAME);require(!h.key('H'),"login scope intercepted");require(h.text().empty(),"login scope changed text");});
    test("scope change during composition preserves existing text",[](Harness& h){h.type("hello");h.input_scope(IS_PASSWORD);require(!h.key('H'),"password transition intercepted");require(h.text()==L"hello","scope transition rewrote existing text");h.input_scope(IS_DEFAULT);h.type("hello");h.key(VK_SPACE);require(h.text()==L"hellohello ","scope transition left stale composition");});
    test("failed text write leaves native key available and recovers",[](Harness& h){h.fail_text(true);require(!h.key('H'),"failed write consumed a key");require(h.text().empty(),"failed write changed text");h.fail_text(false);h.type("hello");h.key(VK_SPACE);require(h.text()==L"hello ","write failure poisoned later composition");});
    test("caret update failure after text write does not duplicate key",[](Harness& h){h.fail_selection(true);require(h.key('H'),"text was written but key passed through");require(h.text()==L"h","written key lost");h.fail_selection(false);h.type("ello");h.key(VK_SPACE);require(h.text()==L"hello ","caret failure duplicated or lost text");});
    test("rejected edit lock does not consume a key",[](Harness& h){h.defer_locks(true);require(!h.key('H'),"lock rejection consumed key");h.defer_locks(false);h.type("hello");h.key(VK_SPACE);require(h.text()==L"hello ","lock rejection damaged later input");});
    test("repeated mixed typing has exact text and measured latency",[](Harness& h){
        std::wstring expected;std::vector<double> timings;
        auto started=std::chrono::steady_clock::now();size_t cycles=0,keys=0;
        do {
        h.clear_text();expected.clear();
        for(int i=0;i<100;++i){
            for(const std::string word:{"hgsghl","hello"}){
                for(char letter:word){auto start=std::chrono::steady_clock::now();require(h.key(static_cast<unsigned>(letter-'a'+'A')),"stress letter lost");double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(timings.size()<10000)timings.push_back(elapsed);else timings[keys%10000]=elapsed;++keys;}
                h.key(VK_SPACE);
            }expected+=L"السلام hello ";
        }
        require(h.text()==expected,"stress text lost or duplicated characters");++cycles;
        }while(soakSeconds && std::chrono::steady_clock::now()-started<std::chrono::seconds(soakSeconds));
        std::sort(timings.begin(),timings.end());
        std::cout<<"METRIC mixed words verified="<<cycles*200<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<'\n';
        std::cout<<"METRIC owned-host key latency p95_ms="<<timings[timings.size()*95/100]<<" p99_ms="<<timings[timings.size()*99/100]<<'\n';
    });
    if(filter.empty())try{Harness host(TF_TMAE_UIELEMENTENABLEDONLY);host.type("hgsghl");host.key(VK_SPACE);require(host.text()==L"السلام ","UI-less mode did not convert");++passed;std::cout<<"PASS UI-less editor activation and word conversion\n";}catch(const std::exception& e){++failed;std::cout<<"FAIL UI-less editor activation: "<<e.what()<<'\n';}
    std::cout<<passed<<" passed, "<<failed<<" failed\n";
    SetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",nullptr);
    // The directory name is fixed to this test's PID under the OS temp directory.
    if(temp.parent_path()==std::filesystem::temp_directory_path())std::filesystem::remove_all(temp);
    CoUninitialize(); return failed?1:0;
}
