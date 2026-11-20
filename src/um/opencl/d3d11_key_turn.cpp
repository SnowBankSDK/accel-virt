#include "um/opencl/d3d11_key_turn.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

D3d11KeyTurn::D3d11KeyTurn(ID3D11Resource &resource) : mutex_(nullptr), acquired_(false)
{
    AV_SCOPE("D3d11KeyTurn::D3d11KeyTurn");
    constexpr UINT64 opened_key = 0u;
    if (FAILED(resource.QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&mutex_)))) {
        mutex_ = nullptr;
        return;
    }
    acquired_ = mutex_->AcquireSync(opened_key, INFINITE) == S_OK;
}

D3d11KeyTurn::~D3d11KeyTurn()
{
    AV_SCOPE("D3d11KeyTurn::~D3d11KeyTurn");
    constexpr UINT64 opened_key = 0u;
    if (acquired_) static_cast<void>(mutex_->ReleaseSync(opened_key));
    if (mutex_ != nullptr) mutex_->Release();
}

bool D3d11KeyTurn::held() const
{
    AV_SCOPE("D3d11KeyTurn::held");
    return mutex_ == nullptr || acquired_;
}

}
