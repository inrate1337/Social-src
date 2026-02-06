#include "Eject.hpp"
#include <NewLight.hpp> 

void Eject::onEnable() {
    Solstice::mRequestEject = true;
    mEnabled = false;
}