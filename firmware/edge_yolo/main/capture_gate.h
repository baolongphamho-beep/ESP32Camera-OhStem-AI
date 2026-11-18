#pragma once
#include <cstdint>

// Protected by the application's capture mutex. Workers finish in-flight work
// normally; their old generation can never publish into a newer scale session.
class CaptureGate {
public:
    bool request(bool enabled, uint32_t sessionId) {
        if (enabled == enabled_ && (!enabled || sessionId == sessionId_)) return false;
        enabled_ = enabled;
        sessionId_ = sessionId;
        ++generation_;
        return true;
    }
    bool beginCapture(uint32_t& token) {
        if (!enabled_ || capturing_) return false;
        capturing_ = true;
        captureToken_ = token = generation_;
        return true;
    }
    bool finishCapture(uint32_t token) {
        if (!capturing_ || token != captureToken_) return false;
        capturing_ = false;
        return accepts(token);
    }
    bool beginInference(uint32_t token) {
        if (inferring_ || !accepts(token)) return false;
        inferring_ = true;
        inferenceToken_ = token;
        return true;
    }
    bool finishInference(uint32_t token) {
        if (!inferring_ || token != inferenceToken_) return false;
        inferring_ = false;
        return accepts(token);
    }
    bool accepts(uint32_t token) const { return enabled_ && token == generation_; }
    bool enabled() const { return enabled_; }
    bool quiescent() const { return !capturing_ && !inferring_; }
    bool canSleep() const { return !enabled_ && quiescent(); }
    uint32_t generation() const { return generation_; }
private:
    bool enabled_ = false, capturing_ = false, inferring_ = false;
    uint32_t sessionId_ = 0, generation_ = 0, captureToken_ = 0, inferenceToken_ = 0;
};
