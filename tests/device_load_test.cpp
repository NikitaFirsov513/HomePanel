#include "DeviceLoadState.hpp"
#include <cassert>
int main() {
    using S = DeviceLoadState;
    assert(deviceLoadState(false, false, false, false, false, false) == S::Starting);
    assert(deviceLoadState(true, false, false, false, false, false) == S::NoAccounts);
    assert(deviceLoadState(true, false, true, false, false, false) == S::NoAccounts);
    assert(deviceLoadState(true, true, false, false, false, false) == S::Offline);
    assert(deviceLoadState(true, true, true, false, false, false) == S::Loading);
    assert(deviceLoadState(true, true, true, true, false, false) == S::Loading);
    assert(deviceLoadState(true, true, true, false, false, true) == S::Error);
    assert(deviceLoadState(true, true, true, false, true, true) == S::Error);
    assert(deviceLoadState(true, true, true, false, true, false) == S::Ready);
    assert(deviceLoadState(true, true, false, false, true, false) == S::Offline);
    assert(deviceLoadState(true, true, true, true, true, false) == S::Loading);
}
