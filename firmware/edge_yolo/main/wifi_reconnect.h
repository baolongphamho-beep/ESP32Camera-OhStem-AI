#pragma once
#include <cstdint>

// Owned by the default event loop. Timer callbacks only post a wakeup event.
class WifiReconnect {
public:
    static constexpr uint32_t DELAY_MS = 2000;

    void enable(bool enabled) { enabled_ = enabled; cancel(); }
    void cancel() { pending_ = false; }
    bool enabled() const { return enabled_; }
    bool pending() const { return pending_; }
    void disconnected(uint32_t now) {
        if (!enabled_) return;
        deadline_ = now + DELAY_MS;
        pending_ = true;
    }
    bool due(uint32_t now) const {
        return pending_ && static_cast<int32_t>(now - deadline_) >= 0;
    }
    uint32_t remaining(uint32_t now) const {
        return !pending_ || due(now) ? 0 : deadline_ - now;
    }
private:
    bool enabled_ = false;
    bool pending_ = false;
    uint32_t deadline_ = 0;
};
