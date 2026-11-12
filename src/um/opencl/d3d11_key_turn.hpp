#ifndef AV_UM_D3D11_KEY_TURN_HPP
#define AV_UM_D3D11_KEY_TURN_HPP

#include <d3d11.h>
#include <dxgi.h>

namespace av {

class D3d11KeyTurn {
public:
    explicit D3d11KeyTurn(ID3D11Resource &resource);
    ~D3d11KeyTurn();
    D3d11KeyTurn(const D3d11KeyTurn &) = delete;
    D3d11KeyTurn &operator=(const D3d11KeyTurn &) = delete;

    bool held() const;

private:
    IDXGIKeyedMutex *mutex_;
    bool             acquired_;
};

}

#endif
