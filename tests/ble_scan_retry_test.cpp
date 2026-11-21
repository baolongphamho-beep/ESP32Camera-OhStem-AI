#include <cassert>
#include <cstdint>
#include <cstdio>
#include "ble_scan_retry.h"

int main() {
    BleScanRetry retry;
    assert(!retry.pending() && !retry.due(0));

    // A fake clock drives two failed starts, followed by a successful start.
    // No retry is permitted before its deadline; no polling loop spins on GAP.
    uint32_t now = 500;
    unsigned starts = 1; // Initial attempt fails.
    assert(retry.failed(now) == 100);
    for (++now; now < 600; ++now) assert(!retry.due(now));
    assert(retry.due(now));
    ++starts; // Second attempt also fails.
    assert(retry.failed(now) == 200);
    assert(!retry.due(799) && retry.due(800));
    ++starts; // Third attempt succeeds.
    retry.clear();
    assert(starts == 3 && !retry.pending() && !retry.due(100000));

    // Success resets backoff; persistent failures are capped at two seconds.
    now = 1000;
    const uint32_t expected[] = {100, 200, 400, 800, 1600, 2000, 2000, 2000};
    for (uint32_t delay : expected) {
        assert(retry.failed(now) == delay);
        assert(!retry.due(now + delay - 1));
        now += delay;
        assert(retry.due(now));
    }

    // A request replaces an old deadline: an already queued old event cannot
    // trigger an extra attempt before the replacement deadline.
    retry.clear();
    assert(retry.failed(100) == 100);
    retry.clear();
    assert(!retry.due(200));
    assert(retry.failed(150) == 100);
    assert(!retry.due(200) && retry.due(250));

    // Millisecond wraparound must neither retry early nor strand a retry.
    retry.clear();
    assert(retry.failed(0xfffffff0U) == 100);
    assert(!retry.due(0xfffffff1U));
    assert(!retry.due(83) && retry.due(84));
    retry.clear();
    assert(!retry.due(84));
    std::puts("ble_scan_retry_test: PASS");
}
