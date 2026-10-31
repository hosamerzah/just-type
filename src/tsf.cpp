#include "tsf.hpp"
#include "global_hook.hpp"
#include <inputscope.h>
#include <array>
#include <functional>
#include <algorithm>
#include <fstream>
#include <memory>
#include "build_identity.hpp"
#include "diagnostics.hpp"
#ifdef __MINGW32__
// These two SDK GUIDs are declared but absent from MinGW's uuid library.
extern "C" const GUID GUID_PROP_INPUTSCOPE = {0x1713dd5a,0x68e7,0x4a5b,{0x9a,0xf6,0x59,0x2a,0x59,0x5c,0x77,0x8d}};
extern "C" const GUID IID_ITfInputScope = {0xfde1eaee,0x6924,0x4cdf,{0x91,0xe7,0xda,0x38,0xcf,0xf5,0x55,0x9d}};
#endif

namespace ak {
class EditSession final : public ITfEditSession {
    std::atomic<ULONG> references_{1};
    std::function<HRESULT(TfEditCookie)> action_;
public:
    explicit EditSession(std::function<HRESULT(TfEditCookie)> action) : action_(std::move(action)) { ++objects; }
    ~EditSession() { --objects; }
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfEditSession) *out = static_cast<ITfEditSession*>(this);
        if (!*out) return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override { auto n = --references_; if (!n) delete this; return n; }
    STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
        try { return action_(cookie); } catch (...) { return E_FAIL; }
    }
};

class TextService final : public ITfTextInputProcessorEx, public ITfKeyEventSink,
                          public ITfCompositionSink, public ITfThreadMgrEventSink {
    std::atomic<ULONG> references_{1};
    ComPtr<ITfThreadMgr> manager_;
    TfClientId client_ = TF_CLIENTID_NULL;
    DWORD threadCookie_ = TF_INVALID_COOKIE;
    ComPtr<ITfContext> context_;
    ComPtr<ITfComposition> composition_;
    ComPtr<ITfRange> wordRange_, lastRange_, lastCaret_;
    std::wstring lastBefore_, lastAfter_;
    std::string lastLanguage_, lastOriginalLanguage_;
    std::string wordInitialLanguage_ = "en";
    std::vector<Stroke> strokes_, lastStrokes_;
    Engine engine_;
    Settings settings_;
    Status status_;
    int64_t revision_ = -1;
    SnapshotLoader snapshots_;
    bool foreground_ = false;
    HWND timer_ = nullptr;
    HHOOK messageHook_=nullptr, keyHook_=nullptr;
    static thread_local TextService* keyHookOwner_;
    static thread_local WPARAM lastPhysicalVk_;
    static thread_local ULONGLONG lastPhysicalTick_;
    static thread_local ULONGLONG lastSinkKeyDownTick_;
    static thread_local TextService* hookOwner_;
    bool routingMessage_=false, sinkCalled_=false;
    bool ready_ = false, finishing_ = false, manualWord_ = false, oversizedWord_=false, startupWord_=false;
    std::array<bool, 256> eaten_{};
    mutable std::optional<DiagnosticConfig> diagnostics_;
    mutable ULONGLONG lastDiagnosticCheck_=0;
    void trace(const char* event,HRESULT result=S_OK,DWORD detail=0) const noexcept {
        try {
            auto now=GetTickCount64();
            if(!diagnostics_ || (!diagnostics_->active() && now-lastDiagnosticCheck_>=1000)){diagnostics_=diagnostic_config();lastDiagnosticCheck_=now;}
            if(!diagnostics_->active())return;
            const auto& path=diagnostics_->file;
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(),error);error.clear();if(std::filesystem::file_size(path,error)>1024*1024 && !error)return;
            std::ofstream out(std::filesystem::path(path),std::ios::app);
            wchar_t exe[32768]{};std::string exeName="unknown";if(GetModuleFileNameW(nullptr,exe,32768))exeName=utf8(std::filesystem::path(exe).filename().wstring());
            out<<GetTickCount64()<<' '<<GetCurrentProcessId()<<' '<<event<<" hr="<<std::hex<<static_cast<unsigned long>(result)<<std::dec<<" detail="<<detail<<" thread="<<GetCurrentThreadId()<<" exe="<<exeName<<" version="<<AK_VERSION<<" build="<<AK_SOURCE_ID<<" bits="<<sizeof(void*)*8<<'\n';
        } catch(...) { }
    }

    void forget_last() { lastRange_.reset(); lastCaret_.reset(); lastStrokes_.clear(); lastBefore_.clear(); lastAfter_.clear(); }
    void publish_status() {
        DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
        if(foregroundPid==GetCurrentProcessId() && GetFocus())status_.publish(engine_);
    }
    void refresh(bool initial=false) noexcept {
        if (!strokes_.empty()) return;
        try {
            auto snapshot=snapshots_.latest(initial);
            if (snapshot && snapshot->revision != revision_) {
                settings_ = snapshot->settings;
                engine_.snapshot_vocabulary = std::shared_ptr<const Vocabulary>(snapshot,&snapshot->vocabulary);
                engine_.load_protected_words(protected_words_path());
                apply(engine_, settings_);
                revision_ = snapshot->revision;
                trace("bank-snapshot-applied");
            }
        } catch (...) { /* Use the last complete snapshot if a bank is unavailable. */ }
    }
    static LRESULT CALLBACK timer_proc(HWND window, UINT msg, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<TextService*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<TextService*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (msg == WM_TIMER && self) {self->refresh();if(self->foreground_)self->publish_status();self->trace("heartbeat",S_OK,self->foreground_);}
        return DefWindowProcW(window, msg, w, l);
    }
    static LRESULT CALLBACK message_proc(int code,WPARAM removed,LPARAM parameter) {
        // Let Windows' normal TSF filters run first. Only still-unhandled raw
        // messages need the compatibility path below.
        LRESULT next=CallNextHookEx(nullptr,code,removed,parameter);
        auto self=hookOwner_;
        if(code!=HC_ACTION || removed!=PM_REMOVE || !self || !self->ready_ || self->routingMessage_)return next;
        auto message=reinterpret_cast<MSG*>(parameter);
        if(!message || (message->message!=WM_KEYDOWN && message->message!=WM_SYSKEYDOWN && message->message!=WM_KEYUP && message->message!=WM_SYSKEYUP))return next;
        self->AddRef();self->routingMessage_=true;
        try {self->route_message(*message);}catch(...){self->trace("message-bridge-exception",E_FAIL);}
        self->routingMessage_=false;self->Release();return next;
    }
    static LRESULT CALLBACK keyboard_proc(int code,WPARAM wParam,LPARAM lParam) {
        auto self=keyHookOwner_;
        if(code==HC_ACTION && self && self->ready_) {
            if(wParam==VK_PROCESSKEY) {
                UINT vsc=(static_cast<UINT>(lParam)>>16)&0xff;
                UINT vk=vsc?MapVirtualKeyW(vsc,MAPVK_VSC_TO_VK_EX):0;
                if(!vk && vsc)vk=MapVirtualKeyW(vsc,MAPVK_VSC_TO_VK);
                if(vk){
                    self->trace("wh-keyboard-processkey",S_OK,vk);
                    bool up=(static_cast<ULONG_PTR>(lParam)&0x80000000u)!=0;
                    if(!up){lastPhysicalVk_=vk;lastPhysicalTick_=GetTickCount64();}
                } else {
                    self->trace("wh-keyboard-processkey",S_OK,0);
                }
            } else {
                bool up=(static_cast<ULONG_PTR>(lParam)&0x80000000u)!=0;
                if(!up){lastPhysicalVk_=wParam;lastPhysicalTick_=GetTickCount64();}
                self->trace("wh-keyboard-key",S_OK,up?0x80000000u|static_cast<DWORD>(wParam):static_cast<DWORD>(wParam));
            }
        }
        return CallNextHookEx(nullptr,code,wParam,lParam);
    }
    void route_message(MSG& message) {
        if(!manager_ || !message.hwnd){trace("bridge-no-window");return;}
        if(message.hwnd!=GetFocus()){trace("bridge-window-not-focused");return;}
        if(message.wParam==VK_PROCESSKEY && GetTickCount64()-lastSinkKeyDownTick_<50){trace("processkey-already-handled-by-sink");lastPhysicalVk_=0;return;}
        if(message.wParam==VK_PROCESSKEY && lastPhysicalVk_ && GetTickCount64()-lastPhysicalTick_<250){trace("processkey-restored-hook",S_OK,static_cast<DWORD>(lastPhysicalVk_));message.wParam=lastPhysicalVk_;lastPhysicalVk_=0;}
        if(message.wParam==VK_PROCESSKEY){UINT vsc=(static_cast<UINT>(message.lParam)>>16)&0xff;UINT vk=vsc?MapVirtualKeyW(vsc,MAPVK_VSC_TO_VK_EX):0;if(!vk && vsc)vk=MapVirtualKeyW(vsc,MAPVK_VSC_TO_VK);if(vk){trace("processkey-restored-vsc",S_OK,vk);message.wParam=vk;}}
        if(message.wParam==VK_PROCESSKEY || message.wParam==VK_PACKET){trace("bridge-nonphysical-message",S_OK,static_cast<DWORD>(message.wParam));return;}
        GUITHREADINFO gui{};gui.cbSize=sizeof(gui);
        if(!GetGUIThreadInfo(GetCurrentThreadId(),&gui) || (gui.flags&(GUI_INMENUMODE|GUI_SYSTEMMENUMODE|GUI_POPUPMENUMODE|GUI_INMOVESIZE))){trace("bridge-menu-or-no-gui-focus");return;}
        ComPtr<ITfKeystrokeMgr> keys;
        HRESULT hr=manager_->QueryInterface(IID_ITfKeystrokeMgr,reinterpret_cast<void**>(keys.put()));if(FAILED(hr)){trace("bridge-no-key-manager",hr);return;}
        CLSID foreground{};
        if(FAILED(keys->GetForeground(&foreground)) || !IsEqualCLSID(foreground,ServiceId)){trace("bridge-other-profile");return;}
        ComPtr<ITfDocumentMgr> document;ComPtr<ITfContext> context;
        if(FAILED(manager_->GetFocus(document.put())) || !document || FAILED(document->GetTop(context.put())) || !context){trace("bridge-no-focused-context");return;}
        if(disabled(context.get())){trace("bridge-disabled-context");return;}
        // Verify the keystroke targets the same top-level host as the TSF
        // context. Windowless contexts (some XAML and browser controls) skip
        // this check since the focused-context and focused-window guards
        // above already apply. Complex hosts may focus a child HWND that is
        // not a direct descendant of the view window but shares its top-level
        // ancestor; allow those too.
        ComPtr<ITfContextView> view;HWND window=nullptr;
        hr=context->GetActiveView(view.put());if(SUCCEEDED(hr) && view)view->GetWnd(&window);
        if(window && window!=message.hwnd && !IsChild(window,message.hwnd) &&
           GetAncestor(window,GA_ROOT)!=GetAncestor(message.hwnd,GA_ROOT)){trace("bridge-wrong-context-window");return;}
        BOOL interested=FALSE,consumed=FALSE;
        bool down=message.message==WM_KEYDOWN || message.message==WM_SYSKEYDOWN;
        if(down) {
            sinkCalled_=false; hr=keys->TestKeyDown(message.wParam,message.lParam,&interested); if(!sinkCalled_)hr=OnTestKeyDown(context.get(),message.wParam,message.lParam,&interested); if(hr!=S_OK || !interested){trace("bridge-key-not-requested",hr,interested);return;}
            sinkCalled_=false; hr=keys->KeyDown(message.wParam,message.lParam,&consumed); if(!sinkCalled_)hr=OnKeyDown(context.get(),message.wParam,message.lParam,&consumed); if(hr!=S_OK){trace("bridge-dispatch-failed",hr);return;}
        } else {
            if(message.wParam>=eaten_.size() || !eaten_[message.wParam])return;
            sinkCalled_=false; hr=keys->TestKeyUp(message.wParam,message.lParam,&interested); if(!sinkCalled_)hr=OnTestKeyUp(context.get(),message.wParam,message.lParam,&interested); if(hr!=S_OK || !interested)return;
            sinkCalled_=false; hr=keys->KeyUp(message.wParam,message.lParam,&consumed); if(!sinkCalled_)hr=OnKeyUp(context.get(),message.wParam,message.lParam,&consumed); if(hr!=S_OK)return;
        }
        trace("message-bridge",S_OK,consumed);
        if(consumed){message.message=WM_NULL;message.wParam=0;message.lParam=0;}
    }
    bool disabled(ITfContext* context) const {
        if (!ready_ || !context) {trace("disabled-not-ready-or-context",S_OK,ready_);return true;}
        if (is_global_hook_active()) return true;
        wchar_t executable[32768]{};
        DWORD n = GetModuleFileNameW(nullptr, executable, 32768);
        if (!n || app_excluded(settings_.excluded_apps, std::filesystem::path(executable).filename().wstring())) {trace("disabled-module-or-exclusion");return true;}
        // Windows may restore the shared East Asian IME open/close compartment
        // after activation. This bilingual keyboard is enabled by selecting its
        // profile; per-field disabled/password/read-only state still applies.
        TF_STATUS state{};
        HRESULT status=context->GetStatus(&state);
        if (FAILED(status) || (state.dwDynamicFlags & TS_SD_READONLY)) {trace("disabled-status",status,state.dwDynamicFlags);return true;}
        ComPtr<ITfCompartmentMgr> compartments;
        if (FAILED(context->QueryInterface(IID_ITfCompartmentMgr, reinterpret_cast<void**>(compartments.put())))) return true;
        for (const auto& id : {GUID_COMPARTMENT_KEYBOARD_DISABLED, GUID_COMPARTMENT_EMPTYCONTEXT}) {
            ComPtr<ITfCompartment> compartment;
            if (SUCCEEDED(compartments->GetCompartment(id, compartment.put()))) {
                VARIANT value; VariantInit(&value);
                HRESULT hr = compartment->GetValue(&value);
                bool blocked = SUCCEEDED(hr) && value.vt == VT_I4 && value.lVal != 0;
                VariantClear(&value);
                if (blocked) {trace("disabled-compartment");return true;}
            }
        }
        ComPtr<ITfContextView> view;
        HWND window = nullptr;
        if (SUCCEEDED(context->GetActiveView(view.put())) && SUCCEEDED(view->GetWnd(&window)) && window) {
            wchar_t cls[128]{}; GetClassNameW(window, cls, 128);
            if ((_wcsicmp(cls, L"Edit") == 0 || wcsstr(cls, L"RichEdit") || wcsstr(cls, L"RICHEDIT")) &&
                (GetWindowLongPtrW(window, GWL_STYLE) & ES_PASSWORD)) return true;
        }
        return false;
    }
    HRESULT edit(ITfContext* context, std::function<HRESULT(TfEditCookie)> action) {
        auto session = new EditSession(std::move(action));
        HRESULT outcome = E_FAIL;
        HRESULT requested = context->RequestEditSession(client_, session, TF_ES_SYNC | TF_ES_READWRITE, &outcome);
        session->Release();
        if(FAILED(requested)||FAILED(outcome))trace("edit-session-failed",FAILED(requested)?requested:outcome);
        return FAILED(requested) ? requested : outcome;
    }
    bool sensitive(TfEditCookie cookie, ITfContext* context) {
        ComPtr<ITfReadOnlyProperty> property;
        if (FAILED(context->GetAppProperty(GUID_PROP_INPUTSCOPE, property.put()))) return false;
        TF_SELECTION selection{}; ULONG fetched = 0;
        if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) || !fetched) return true;
        VARIANT value; VariantInit(&value);
        HRESULT result = property->GetValue(cookie, selection.range, &value);
        selection.range->Release();
        bool blocked = false;
        if (SUCCEEDED(result) && value.vt == VT_UNKNOWN && value.punkVal) {
            ComPtr<ITfInputScope> scope;
            if (SUCCEEDED(value.punkVal->QueryInterface(IID_ITfInputScope, reinterpret_cast<void**>(scope.put())))) {
                InputScope* scopes = nullptr; UINT count = 0;
                if (SUCCEEDED(scope->GetInputScopes(&scopes, &count))) {
                    for (UINT i = 0; i < count; ++i) if (scopes[i] == IS_PASSWORD || scopes[i] == IS_EMAIL_USERNAME ||
                        scopes[i] == IS_EMAIL_SMTPEMAILADDRESS || scopes[i] == IS_URL || scopes[i] == IS_LOGINNAME || static_cast<int>(scopes[i]) == 64) blocked = true;
                    CoTaskMemFree(scopes);
                }
            }
        }
        VariantClear(&value);
        return blocked;
    }
    HRESULT caret(TfEditCookie cookie, ITfContext* context, ITfRange* range) {
        ComPtr<ITfRange> end;
        HRESULT hr = range->Clone(end.put());
        if (FAILED(hr)) return hr;
        if (FAILED(hr = end->Collapse(cookie, TF_ANCHOR_END))) return hr;
        TF_SELECTION selection{end.get(), {TF_AE_NONE, FALSE}};
        return context->SetSelection(cookie, 1, &selection);
    }
    bool same_caret(TfEditCookie cookie, ITfContext* context, ITfRange* expected) {
        if (!expected) return false;
        TF_SELECTION selection{}; ULONG fetched = 0;
        if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) || !fetched) return false;
        LONG a = 1, b = 1;
        HRESULT h1 = selection.range->CompareStart(cookie, expected, TF_ANCHOR_START, &a);
        HRESULT h2 = selection.range->CompareEnd(cookie, expected, TF_ANCHOR_END, &b);
        selection.range->Release();
        return SUCCEEDED(h1) && SUCCEEDED(h2) && a == 0 && b == 0;
    }
    bool range_text(TfEditCookie cookie, ITfRange* range, const std::wstring& expected) {
        if (!range) return false;
        std::wstring text(expected.size() + 1, 0); ULONG count = 0;
        return SUCCEEDED(range->GetText(cookie, 0, text.data(), static_cast<ULONG>(text.size()), &count)) &&
               count == expected.size() && std::equal(expected.begin(), expected.end(), text.begin());
    }
    HRESULT update(TfEditCookie cookie, ITfContext* context, const std::wstring& text) {
        HRESULT hr;
        if (!composition_ && !wordRange_) {
            TF_SELECTION selection{}; ULONG fetched = 0;
            if (FAILED(hr = context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) || !fetched) return E_FAIL;
            selection.range->Clone(wordRange_.put()); if (wordRange_) wordRange_->SetGravity(cookie, TF_GRAVITY_BACKWARD, TF_GRAVITY_FORWARD);
            ComPtr<ITfContextComposition> compositions;
            hr = context->QueryInterface(IID_ITfContextComposition, reinterpret_cast<void**>(compositions.put()));
            if (SUCCEEDED(hr)) hr = compositions->StartComposition(cookie, selection.range, this, composition_.put());
            selection.range->Release();
            if ((FAILED(hr) || !composition_) && !wordRange_) return FAILED(hr) ? hr : E_FAIL;
            context_ = ComPtr<ITfContext>(context);
        }
        ComPtr<ITfRange> range;
        if (composition_) hr = composition_->GetRange(range.put()); else if (wordRange_) hr = wordRange_->Clone(range.put()); else return E_FAIL;
        if (FAILED(hr)) return hr;
        if (FAILED(hr = range->SetText(cookie, 0, text.data(), static_cast<LONG>(text.size())))) return hr;
        range->Clone(wordRange_.put()); if (wordRange_) wordRange_->SetGravity(cookie, TF_GRAVITY_BACKWARD, TF_GRAVITY_FORWARD);
        // Once SetText succeeds the key is consumed even if the host refuses a
        // caret update. Returning a failure here would insert the key twice.
        caret(cookie, context, range.get());
        return S_OK;
    }
    HRESULT finish(TfEditCookie cookie, ITfContext* context, bool detect, const std::wstring& delimiter = L"") {
        if (!composition_ && !wordRange_) return S_FALSE;
        auto decision = detect && !manualWord_ ? engine_.decide(strokes_) : Decision{engine_.render(strokes_, engine_.current), engine_.current, false, "commit"};
        auto original = engine_.render(strokes_, wordInitialLanguage_);
        auto oldLanguage = wordInitialLanguage_;
        ComPtr<ITfRange> range;
        HRESULT hr = composition_ ? composition_->GetRange(range.put()) : (wordRange_ ? wordRange_->Clone(range.put()) : E_FAIL);
        if (FAILED(hr)) return hr;
        if (FAILED(hr = range->SetText(cookie, 0, decision.text.data(), static_cast<LONG>(decision.text.size())))) return hr;
        caret(cookie, context, range.get());
        forget_last(); wordRange_.reset();
        range->Clone(lastRange_.put());
        if (lastRange_) lastRange_->SetGravity(cookie, TF_GRAVITY_FORWARD, TF_GRAVITY_BACKWARD);
        lastStrokes_ = strokes_; lastBefore_ = original; lastAfter_ = decision.text;
        lastLanguage_ = decision.language; lastOriginalLanguage_ = oldLanguage;
        finishing_ = true;
        hr = composition_ ? composition_->EndComposition(cookie) : S_OK;
        finishing_ = false;
        if (FAILED(hr)) {
            // The replacement is already visible. Retire this composition and
            // never render the old strokes over it on a later key.
            trace("end-composition-failed",hr);abandon_context();return S_FALSE;
        }
        composition_.reset(); wordRange_.reset(); strokes_.clear(); manualWord_ = false;
        engine_.current = decision.language;
        publish_status();
        if (!delimiter.empty()) {
            ComPtr<ITfInsertAtSelection> insertion;
            ComPtr<ITfRange> added;
            if (SUCCEEDED(context->QueryInterface(IID_ITfInsertAtSelection, reinterpret_cast<void**>(insertion.put()))) &&
                SUCCEEDED(insertion->InsertTextAtSelection(cookie, 0, delimiter.data(), static_cast<LONG>(delimiter.size()), added.put()))) {
                caret(cookie, context, added.get());
            } else {
                // Word text has already been committed. Let the delimiter pass
                // through instead of claiming that it was inserted.
                forget_last(); return S_FALSE;
            }
        }
        TF_SELECTION selection{}; ULONG fetched = 0;
        if (SUCCEEDED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) && fetched) {
            lastCaret_ = ComPtr<ITfRange>(selection.range); selection.range->Release();
        }
        return S_OK;
    }
    void abandon_context() {
        forget_last(); wordRange_.reset();
        auto context=std::move(context_);
        auto pending=std::make_shared<ComPtr<ITfComposition>>(std::move(composition_));
        strokes_.clear();manualWord_=false;oversizedWord_=false;startupWord_=false;
        if(!context || !*pending)return;
        // Focus/context notifications need not permit a synchronous write lock.
        // End the old composition whenever its owner grants access. The text
        // already shown is preserved; this callback never reads current state
        // or rewrites a different field after the user has moved on.
        auto session=new EditSession([pending](TfEditCookie cookie) {
            return (*pending)->EndComposition(cookie);
        });
        HRESULT outcome=E_FAIL;
        HRESULT requested=context->RequestEditSession(client_,session,TF_ES_ASYNCDONTCARE|TF_ES_READWRITE,&outcome);
        session->Release();
        if(FAILED(requested)||FAILED(outcome))trace("end-old-composition-failed",FAILED(requested)?requested:outcome);
    }
    static bool same_object(IUnknown* a,IUnknown* b) {
        if(!a || !b)return a==b;
        ComPtr<IUnknown> first,second;
        return SUCCEEDED(a->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(first.put()))) &&
            SUCCEEDED(b->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(second.put()))) && first.get()==second.get();
    }
    bool is_focused_context() const {
        if(!manager_ || !context_)return false;
        ComPtr<ITfDocumentMgr> document;ComPtr<ITfContext> top;
        return SUCCEEDED(manager_->GetFocus(document.put())) && document &&
            SUCCEEDED(document->GetTop(top.put())) && same_object(top.get(),context_.get());
    }
    HRESULT convert_last(TfEditCookie cookie, ITfContext* context, bool undo) {
        if (!lastRange_ || !same_caret(cookie, context, lastCaret_.get()) || !range_text(cookie, lastRange_.get(), lastAfter_)) {
            forget_last(); if(strokes_.empty())wordRange_.reset(); return S_FALSE;
        }
        auto decision = undo ? Decision{lastBefore_, lastOriginalLanguage_, true, "undo"} : engine_.manual(lastStrokes_, lastLanguage_);
        HRESULT hr = lastRange_->SetText(cookie, 0, decision.text.data(), static_cast<LONG>(decision.text.size()));
        if (FAILED(hr)) return hr;
        engine_.current = decision.language;
        lastLanguage_ = decision.language; lastAfter_ = decision.text;
        // The stored caret range tracks the trailing delimiter when lengths change.
        TF_SELECTION selection{lastCaret_.get(), {TF_AE_NONE, FALSE}};
        context->SetSelection(cookie, 1, &selection);
        publish_status();
        if (undo) forget_last();
        return S_OK;
    }
    bool interesting(WPARAM key) const {
        if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU || key == VK_CAPITAL ||
            key == VK_LSHIFT || key == VK_RSHIFT || key == VK_LCONTROL || key == VK_RCONTROL ||
            key == VK_LMENU || key == VK_RMENU || key == VK_LWIN || key == VK_RWIN ||
            key == VK_NUMLOCK || key == VK_SCROLL) return false;
        bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        bool win = (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000);
        if (control && alt && !win && (key == VK_SPACE || key == VK_BACK || key == 'L')) return true;
        if (control || alt || win) return static_cast<bool>(composition_) || static_cast<bool>(wordRange_) || static_cast<bool>(lastRange_);
        if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU || key == VK_CAPITAL || key == VK_LSHIFT || key == VK_RSHIFT) return false;
        return engine_.mapped(static_cast<unsigned>(key)) || startupWord_ || oversizedWord_ || static_cast<bool>(composition_) || static_cast<bool>(wordRange_) || static_cast<bool>(lastRange_);
    }
    HRESULT key(ITfContext* context, WPARAM key_, BOOL* consumed,unsigned repeats=1) {
        *consumed = FALSE;
        if (disabled(context) || !interesting(key_)) return S_OK;
        refresh();
        if (context_ && !same_object(context_.get(),context)) abandon_context();
        bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        bool win = (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000);
        bool shortcut = control && alt && !win;
        Stroke stroke{static_cast<unsigned>(key_), (GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_CAPITAL) & 1) != 0};
        if(revision_<0 && snapshots_.pending()){startupWord_=true;trace("bank-initial-load-pending",S_FALSE);}
        // Never begin converting a suffix after an earlier part of this word
        // passed through while the initial settings were still loading.
        if(startupWord_){if(engine_.boundary(stroke) || (!engine_.mapped(stroke.key) && key_!=VK_BACK))startupWord_=false;return S_OK;}
        HRESULT result = edit(context, [&](TfEditCookie cookie) -> HRESULT {
            if (sensitive(cookie, context)) { abandon_context();trace("sensitive-context");return S_FALSE; }
            if (composition_ || wordRange_) {
                ComPtr<ITfRange> range, end;
                if ((composition_ ? SUCCEEDED(composition_->GetRange(range.put())) : SUCCEEDED(wordRange_->Clone(range.put()))) && SUCCEEDED(range->Clone(end.put()))) {
                    end->Collapse(cookie, TF_ANCHOR_END);
                    if (!same_caret(cookie, context, end.get()) || !range_text(cookie,range.get(),engine_.render(strokes_,engine_.current))) {
                        // Preserve the host's text and selection after mouse or
                        // external edits. Never overwrite them with stale keys.
                        abandon_context();
                    }
                }
            }
            if (shortcut && key_ == 'L') {
                engine_.locked = !engine_.locked; settings_.locked = engine_.locked;
                if (HWND tray = FindWindowW(tray_class().c_str(), nullptr)) PostMessageW(tray, TrayLockMessage, engine_.locked, 0);
                publish_status(); *consumed = TRUE; return S_OK;
            }
            if (shortcut && key_ == VK_SPACE) {
                if (composition_ || wordRange_) {
                    auto decision = engine_.manual(strokes_, engine_.current);
                    auto previous = engine_.current;
                    engine_.current = decision.language;
                    HRESULT hr = update(cookie, context, decision.text);
                    if (FAILED(hr)) engine_.current = previous;
                    else { manualWord_ = true; publish_status(); *consumed = TRUE; }
                    return hr;
                }
                if (convert_last(cookie, context, false) == S_OK) *consumed = TRUE;
                return S_OK;
            }
            if (shortcut && key_ == VK_BACK) {
                if((composition_ || wordRange_) && manualWord_) {
                    auto previous=engine_.current; engine_.current=wordInitialLanguage_;
                    HRESULT hr=update(cookie,context,engine_.render(strokes_,engine_.current));
                    if(FAILED(hr))engine_.current=previous;
                    else{manualWord_=false;publish_status();*consumed=TRUE;}
                    return hr;
                }
                if (convert_last(cookie, context, true) == S_OK) *consumed = TRUE;
                return S_OK;
            }
            if (control || alt || win || key_ == VK_LEFT || key_ == VK_RIGHT || key_ == VK_UP || key_ == VK_DOWN ||
                key_ == VK_HOME || key_ == VK_END || key_ == VK_DELETE || key_ == VK_TAB || key_ == VK_ESCAPE || (key_ >= VK_F1 && key_ <= VK_F24)) {
                if (composition_ || wordRange_) finish(cookie, context, false);
                forget_last();oversizedWord_=false; return S_OK;
            }
            auto insert_direct=[&](const std::wstring& text)->HRESULT {
                ComPtr<ITfInsertAtSelection> insertion;ComPtr<ITfRange> added;
                HRESULT hr=context->QueryInterface(IID_ITfInsertAtSelection,reinterpret_cast<void**>(insertion.put()));
                if(SUCCEEDED(hr))hr=insertion->InsertTextAtSelection(cookie,0,text.data(),static_cast<LONG>(text.size()),added.put());
                if(SUCCEEDED(hr)){*consumed=TRUE;caret(cookie,context,added.get());}return hr;
            };
            auto repeated_text=[&]{std::wstring result;auto unit=engine_.render({stroke},engine_.current);for(unsigned i=0;i<repeats;++i)result+=unit;return result;};
            if(oversizedWord_){
                if(engine_.boundary(stroke)){oversizedWord_=false;if(key_==VK_SPACE || key_==VK_RETURN)return S_OK;return insert_direct(repeated_text());}
                if(key_==VK_BACK)return S_OK;
                if(engine_.mapped(stroke.key))return insert_direct(repeated_text());
                oversizedWord_=false;return S_OK;
            }
            if (key_ == VK_BACK) {
                forget_last();
                if(repeats>1){if(composition_||wordRange_)finish(cookie,context,false);forget_last();wordRange_.reset();return S_OK;}
                if (strokes_.empty()) return S_OK;
                auto old = strokes_; strokes_.pop_back();
                HRESULT hr = update(cookie, context, engine_.render(strokes_, engine_.current));
                if (FAILED(hr)) { strokes_ = std::move(old); return hr; }
                *consumed = TRUE;
                if (strokes_.empty()) { finish(cookie, context, false); forget_last(); }
                return S_OK;
            }
            if (engine_.boundary(stroke)) {
                if (!composition_ && !wordRange_) {
                    forget_last();
                    if (key_ == VK_SPACE || key_ == VK_RETURN) return S_OK;
                    auto text = repeated_text();
                    ComPtr<ITfInsertAtSelection> insertion;
                    ComPtr<ITfRange> inserted;
                    if (SUCCEEDED(context->QueryInterface(IID_ITfInsertAtSelection,reinterpret_cast<void**>(insertion.put()))) &&
                        SUCCEEDED(insertion->InsertTextAtSelection(cookie,0,text.data(),static_cast<LONG>(text.size()),inserted.put()))) {
                        caret(cookie,context,inserted.get()); *consumed=TRUE;
                    }
                    return S_OK;
                }
                // Enter is passed to the host exactly once, after synchronous
                // commitment, preserving Send/Submit and multiline behavior.
                std::wstring delimiter = key_ == VK_RETURN ? L"" : key_ == VK_SPACE ? std::wstring(repeats,L' ') : repeated_text();
                HRESULT hr = finish(cookie, context, true, delimiter);
                *consumed = key_ != VK_RETURN && hr == S_OK;
                if (key_ == VK_RETURN) forget_last();
                return hr;
            }
            if (!engine_.mapped(stroke.key)) {
                if (composition_ || wordRange_) finish(cookie, context, false);
                forget_last(); return S_OK;
            }
            // If the caret moved inside a composition, commit its current text
            // without detection and start the new word at the selected position.
            if (composition_ || wordRange_) {
                ComPtr<ITfRange> range, end;
                if ((composition_ ? SUCCEEDED(composition_->GetRange(range.put())) : SUCCEEDED(wordRange_->Clone(range.put()))) && SUCCEEDED(range->Clone(end.put()))) {
                    end->Collapse(cookie, TF_ANCHOR_END);
                    if (!same_caret(cookie, context, end.get())) {
                        if (composition_) { finishing_ = true; composition_->EndComposition(cookie); finishing_ = false; }
                        composition_.reset(); wordRange_.reset(); strokes_.clear();
                    }
                }
            }
            forget_last(); if(strokes_.empty())wordRange_.reset();
            if(strokes_.empty())wordInitialLanguage_=engine_.current;
            if (strokes_.size()+repeats > 256) {
                HRESULT hr=(composition_||wordRange_)?finish(cookie,context,false):S_OK;forget_last();wordRange_.reset();
                if(hr!=S_OK)return hr;
                oversizedWord_=true;return insert_direct(repeated_text());
            }
            auto before=strokes_.size();strokes_.insert(strokes_.end(),repeats,stroke);
            HRESULT hr = update(cookie, context, engine_.render(strokes_, engine_.current));
            if (FAILED(hr)) { strokes_.resize(before); return hr; }
            *consumed = TRUE; publish_status(); return S_OK;
        });
        if(FAILED(result))trace("key-edit-rejected",result);
        return S_OK;
    }
public:
    TextService() { ++objects; }
    ~TextService() { Deactivate(); --objects; }
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor || iid == IID_ITfTextInputProcessorEx) *out = static_cast<ITfTextInputProcessorEx*>(this);
        else if (iid == IID_ITfKeyEventSink) *out = static_cast<ITfKeyEventSink*>(this);
        else if (iid == IID_ITfCompositionSink) *out = static_cast<ITfCompositionSink*>(this);
        else if (iid == IID_ITfThreadMgrEventSink) *out = static_cast<ITfThreadMgrEventSink*>(this);
        if (!*out) return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override { auto n = --references_; if (!n) delete this; return n; }
    STDMETHODIMP Activate(ITfThreadMgr* manager, TfClientId client) override { return ActivateEx(manager, client, 0); }
    STDMETHODIMP ActivateEx(ITfThreadMgr* manager, TfClientId client, DWORD flags) override {
        diagnostics_.reset();
        trace("activate",S_OK,flags);
        if (!manager) return E_INVALIDARG;
        Deactivate();
        // UI-less mode is not secure mode. This service has no candidate window,
        // so it can compose normally when an editor requests UI-less input.
        if (flags & TF_TMAE_SECUREMODE) return E_FAIL;
        try {
            engine_.load(packs_folder());
            revision_=-1;
            snapshots_.start(data_folder()/L"words.db");refresh(true);
            if(revision_<0)trace("bank-unavailable-retaining-settings",S_FALSE);
            manager_ = ComPtr<ITfThreadMgr>(manager); client_ = client;
            ComPtr<ITfKeystrokeMgr> keys;
            HRESULT hr = manager->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(keys.put()));
            if (FAILED(hr) || FAILED(hr = keys->AdviseKeyEventSink(client_, this, TRUE))) {trace("advise-failed",hr); Deactivate(); return hr; }
            ComPtr<ITfSource> source;
            hr=manager->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(source.put()));
            if(SUCCEEDED(hr))hr=source->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &threadCookie_);
            if(FAILED(hr)){trace("advise-focus-failed",hr);Deactivate();return hr;}
            WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = timer_proc; cls.lpszClassName = L"AutoKeyboard.Snapshot.1";
            RegisterClassW(&cls);
            timer_ = CreateWindowW(cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
            if (timer_) SetTimer(timer_, 1, 1500, nullptr);
            ready_ = true;
            // Selecting this profile must enable composition as well as install
            // the key sink. RichEdit/IMM compatibility can bypass a closed IME.
            // Follow InitializeSampleIMECompartment in Microsoft's SampleIME.
            ComPtr<ITfCompartmentMgr> compartments;
            ComPtr<ITfCompartment> open;
            hr=manager_->QueryInterface(IID_ITfCompartmentMgr,reinterpret_cast<void**>(compartments.put()));
            if(SUCCEEDED(hr))hr=compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,open.put());
            if(SUCCEEDED(hr)) {
                VARIANT value;VariantInit(&value);value.vt=VT_I4;value.lVal=1;
                hr=open->SetValue(client_,&value);
            }
            trace("open-keyboard",hr);
            trace("ready");
            // The thread-local message bridge supplements native TSF delivery
            // for hosts that do not forward keys to the keystroke manager.
            // Native TSF converts WM_KEYDOWN to VK_PROCESSKEY when it
            // consumes a key, so the bridge skips already-handled messages.
            // Set AUTOKEYBOARD_MESSAGE_BRIDGE=0 to disable it.
            wchar_t bridge[2]{};
            DWORD bridgeLen=GetEnvironmentVariableW(L"AUTOKEYBOARD_MESSAGE_BRIDGE",bridge,2);
            bool bridgeEnabled=!(bridgeLen==1 && bridge[0]==L'0');
            if(diagnostics_ && diagnostics_->message_bridge)bridgeEnabled=*diagnostics_->message_bridge;
            if(!hookOwner_ && bridgeEnabled) {
                messageHook_=SetWindowsHookExW(WH_GETMESSAGE,message_proc,nullptr,GetCurrentThreadId());
                if(messageHook_)hookOwner_=this;
                trace("message-bridge-installed",messageHook_?S_OK:HRESULT_FROM_WIN32(GetLastError()));
                if(!keyHookOwner_){keyHook_=SetWindowsHookExW(WH_KEYBOARD,keyboard_proc,nullptr,GetCurrentThreadId());if(keyHook_)keyHookOwner_=this;trace("keyboard-hook-installed",keyHook_?S_OK:HRESULT_FROM_WIN32(GetLastError()));}
            }
            BOOL focused=FALSE;manager_->IsThreadFocus(&focused);foreground_=focused!=FALSE;
            if(foreground_)publish_status();
            return S_OK;
        } catch (...) { Deactivate(); return E_FAIL; }
    }
    STDMETHODIMP Deactivate() override {
        trace("deactivate");
        ready_ = false;
        foreground_=false;status_.clear();snapshots_.stop();
        if(messageHook_){UnhookWindowsHookEx(messageHook_);messageHook_=nullptr;}
        if(keyHook_){UnhookWindowsHookEx(keyHook_);keyHook_=nullptr;}
        if(keyHookOwner_==this)keyHookOwner_=nullptr;
        if(hookOwner_==this)hookOwner_=nullptr;
        if (timer_) { KillTimer(timer_, 1); DestroyWindow(timer_); timer_ = nullptr; }
        UnregisterClassW(L"AutoKeyboard.Snapshot.1",instance);
        abandon_context();
        if (manager_) {
            ComPtr<ITfKeystrokeMgr> keys;
            if (SUCCEEDED(manager_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(keys.put())))) keys->UnadviseKeyEventSink(client_);
            ComPtr<ITfSource> source;
            if (threadCookie_ != TF_INVALID_COOKIE && SUCCEEDED(manager_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(source.put())))) source->UnadviseSink(threadCookie_);
        }
        threadCookie_ = TF_INVALID_COOKIE; client_ = TF_CLIENTID_NULL;
        manager_.reset(); eaten_.fill(false);
        return S_OK;
    }
    STDMETHODIMP OnSetFocus(BOOL foreground) override {
        try {trace("key-focus",S_OK,foreground);foreground_=foreground!=FALSE;
            if(!foreground){status_.clear();abandon_context();}else publish_status();
        }catch(...){trace("focus-exception",E_FAIL);}return S_OK;
    }
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM key_, LPARAM, BOOL* consumed) override {
        if (!consumed) return E_POINTER;
        *consumed=FALSE;sinkCalled_=true;try{*consumed = !disabled(context) && interesting(key_);trace("test-key",S_OK,*consumed);}catch(...){trace("test-key-exception",E_FAIL);} return S_OK;
    }
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM key_, LPARAM flags, BOOL* consumed) override {
        if (!consumed) return E_POINTER;
        try { sinkCalled_=true; lastSinkKeyDownTick_=GetTickCount64(); key(context, key_, consumed,std::max<unsigned>(1,static_cast<unsigned>(flags)&0xffff));trace("key-down",S_OK,*consumed);if(*consumed)foreground_=true; if (key_ < eaten_.size()) eaten_[key_] = *consumed != FALSE; }
        catch (...) { *consumed = FALSE; }
        return S_OK;
    }
    STDMETHODIMP OnTestKeyUp(ITfContext*, WPARAM key_, LPARAM, BOOL* consumed) override {
        if (!consumed) return E_POINTER;
        sinkCalled_=true; *consumed = key_ < eaten_.size() && eaten_[key_]; return S_OK;
    }
    STDMETHODIMP OnKeyUp(ITfContext*, WPARAM key_, LPARAM, BOOL* consumed) override {
        if (!consumed) return E_POINTER;
        sinkCalled_=true; *consumed = key_ < eaten_.size() && eaten_[key_];
        if (key_ < eaten_.size()) eaten_[key_] = false;
        return S_OK;
    }
    STDMETHODIMP OnPreservedKey(ITfContext*, REFGUID, BOOL* consumed) override { if (!consumed) return E_POINTER; *consumed = FALSE; return S_OK; }
    STDMETHODIMP OnCompositionTerminated(TfEditCookie, ITfComposition* composition) override {
        trace("composition-terminated",S_OK,finishing_);
        if (composition_.get() == composition && !finishing_) { composition_.reset(); }
        return S_OK;
    }
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* document) override {
        try{if(context_) {ComPtr<ITfDocumentMgr> owner;if(SUCCEEDED(context_->GetDocumentMgr(owner.put())) && same_object(owner.get(),document))abandon_context();}}catch(...){trace("uninit-context-exception",E_FAIL);}
        return S_OK;
    }
    STDMETHODIMP OnSetFocus(ITfDocumentMgr*, ITfDocumentMgr*) override { try{trace("document-focus");if(context_&&!is_focused_context())abandon_context();}catch(...){trace("document-focus-exception",E_FAIL);}return S_OK; }
    STDMETHODIMP OnPushContext(ITfContext*) override { try{trace("push-context");if(context_&&!is_focused_context())abandon_context();}catch(...){trace("push-context-exception",E_FAIL);}return S_OK; }
    STDMETHODIMP OnPopContext(ITfContext* context) override { try{trace("pop-context");if(context_&&same_object(context_.get(),context))abandon_context();}catch(...){trace("pop-context-exception",E_FAIL);}return S_OK; }
};
thread_local TextService* TextService::hookOwner_=nullptr;
thread_local TextService* TextService::keyHookOwner_=nullptr;
thread_local WPARAM TextService::lastPhysicalVk_=0;
thread_local ULONGLONG TextService::lastPhysicalTick_=0;
thread_local ULONGLONG TextService::lastSinkKeyDownTick_=0;
HRESULT create_service(REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    try { auto service = new TextService; auto hr = service->QueryInterface(iid, out); service->Release(); return hr; }
    catch (...) { return E_OUTOFMEMORY; }
}
}
