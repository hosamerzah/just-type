#pragma once
#include "runtime.hpp"
#include "win_compat.hpp"
#include <atomic>
#include <utility>

namespace ak {
extern HINSTANCE instance;
extern std::atomic<long> objects;
extern std::atomic<long> locks;
HRESULT create_service(REFIID iid, void** out);
template<class T> class ComPtr {
    T* value_ = nullptr;
public:
    ComPtr() = default;
    explicit ComPtr(T* p) : value_(p) { if (p) p->AddRef(); }
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    ComPtr& operator=(ComPtr&& other) noexcept { reset(); value_ = std::exchange(other.value_, nullptr); return *this; }
    T* get() const { return value_; }
    T* operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    T** put() { reset(); return &value_; }
    void reset() { if (value_) { auto p = std::exchange(value_, nullptr); p->Release(); } }
};
}
