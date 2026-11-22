#pragma once
#include <atomic>
#include <cstdint>

// RAM-only preview request. The viewer renews every 3 s; loss of the USB client
// returns to normal Plan 2 after 10 s, without keeping a test flag in NVS.
class CameraTestLease {
public:
    static constexpr uint32_t timeoutMs = 10000;
    void renew(uint32_t now) { at_.store(now); enabled_.store(true); }
    void stop() { enabled_.store(false); }
    bool active(uint32_t now) const {
        return enabled_.load() && uint32_t(now - at_.load()) < timeoutMs;
    }
private:
    std::atomic<uint32_t> at_{0};
    std::atomic<bool> enabled_{false};
};
