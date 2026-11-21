#include <cassert>
#include <cstdio>
#include "wifi_reconnect.h"

int main() {
    WifiReconnect policy;
    assert(!policy.enabled() && !policy.pending());
    policy.disconnected(0);
    assert(!policy.due(2000)); // No SSID: never reconnect.

    policy.enable(true);
    policy.disconnected(100);
    assert(policy.pending() && policy.remaining(100) == 2000);
    assert(!policy.due(2099) && policy.due(2100));
    assert(policy.remaining(2100) == 0);

    // Consume one retry; association/DHCP success cancels all queued wakeups.
    unsigned connects = 0;
    if (policy.due(2100)) { policy.cancel(); ++connects; }
    assert(connects == 1 && !policy.due(2200));
    policy.disconnected(3000);
    policy.cancel();
    assert(!policy.due(5000));

    // An API connect failure schedules a fresh delay, never a tight loop.
    policy.disconnected(6000);
    assert(!policy.due(6000) && policy.due(8000));
    policy.cancel();
    policy.disconnected(8000);
    assert(!policy.due(9999) && policy.due(10000));

    // Config replacement invalidates old retries. A stale wakeup cannot use
    // its previous deadline to connect the new network prematurely.
    policy.disconnected(11000);
    policy.enable(true);
    assert(!policy.pending() && !policy.due(13000));
    policy.disconnected(12000);
    assert(!policy.due(13000) && policy.remaining(13000) == 1000);
    assert(policy.due(14000));

    // Clearing SSID / stopping disables retry even if a timer event is queued.
    policy.enable(false);
    assert(!policy.due(14000));
    policy.disconnected(15000);
    assert(!policy.pending());

    // Repeated disconnects replace the pending deadline instead of stacking.
    policy.enable(true);
    policy.disconnected(16000);
    policy.disconnected(17000);
    assert(!policy.due(18000) && policy.due(19000));

    policy.disconnected(0xfffffff0U);
    assert(!policy.due(1983) && policy.due(1984));
    std::puts("wifi_reconnect_test: PASS");
}
