#pragma once
#include <cstdint>

// Host-task-owned retry timing; independent of NimBLE for desktop tests.
class BleScanRetry {
public:
    uint32_t failed(uint32_t now) {
        const uint32_t delay = nextDelay_;
        deadline_ = now + delay;
        pending_ = true;
        nextDelay_ = delay < 1000 ? delay * 2 : 2000;
        return delay;
    }

    bool due(uint32_t now) const {
        return pending_ && static_cast<int32_t>(now - deadline_) >= 0;
    }

    void clear() {
        pending_ = false;
        nextDelay_ = 100;
    }

    bool pending() const { return pending_; }

private:
    uint32_t nextDelay_ = 100;
    uint32_t deadline_ = 0;
    bool pending_ = false;
};
