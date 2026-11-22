#include "../firmware/edge_yolo/main/edge_geometry.h"
#include <cassert>
#include <string>

int main() {
    // 640x480 is letterboxed to 224x168, with 28px top padding.
    const auto b = originalBox({56, 56, 168, 168, .9f}, 640, 480);
    assert(std::fabs(b.x1 - 160) < .001f && std::fabs(b.y1 - 80) < .001f);
    assert(std::fabs(b.x2 - 480) < .001f && std::fabs(b.y2 - 400) < .001f);
    assert(fullBox(b, 640, 480));
    assert(!fullBox({0, 10, 200, 400, .9f}, 640, 480));
    float cm, distance;
    // Level camera 1m above ground, fy=600px, person at z=3m.
    // feet=cy+200, top=cy-160 => person=1.8m.
    assert(groundHeight({269.5f, 79.5f, 369.5f, 439.5f, .9f},
                        640, 480, 600, 100, 0, cm, distance));
    assert(std::fabs(cm - 180) < .001f && std::fabs(distance - 3) < .001f);
    assert(!groundHeight({200, 20, 400, 200, .9f}, 640, 480, 600, 100, 0, cm, distance));
    assert(!groundHeight(b, 640, 480, 0, 100, 0, cm, distance));

    // Guard against invalid or extreme aspect ratio dimensions (no divide-by-zero).
    const auto invalid = originalBox({10, 10, 20, 20, .9f}, 0, 480);
    assert(invalid.x1 == 0 && invalid.x2 == 0);
    const auto extreme = originalBox({110, 10, 114, 210, .9f}, 1, 1000);
    assert(std::isfinite(extreme.x1) && std::isfinite(extreme.y2));

    // Median filter suppresses single-frame box jitter / outliers.
    HeightMedianFilter filter;
    float medCm = 0, medDist = 0;
    assert(!filter.median(medCm, medDist));
    filter.push(170.0f, 2.50f);
    assert(filter.median(medCm, medDist) && std::fabs(medCm - 170.0f) < .001f);
    filter.push(171.0f, 2.52f);
    filter.push(230.0f, 2.51f); // Outlier
    filter.push(169.0f, 2.49f);
    filter.push(170.5f, 2.505f);
    assert(filter.count() == 5);
    assert(filter.median(medCm, medDist));
    assert(std::fabs(medCm - 170.5f) < .001f && std::fabs(medDist - 2.505f) < .001f);
    filter.reset();
    assert(filter.count() == 0 && !filter.median(medCm, medDist));

    // Auto-calibration requires 3 consistent frames within 5% spread.
    AutoCalibCollector calib;
    float medBoxH = 0;
    assert(!calib.add(300.0f, medBoxH) && calib.count() == 1);
    assert(!calib.add(-10.0f, medBoxH) && calib.count() == 1);
    assert(!calib.add(360.0f, medBoxH) && calib.count() == 2); // Jump > 5%
    assert(!calib.add(302.0f, medBoxH) && calib.count() == 3); // Window [300, 360, 302] -> spread 20%
    assert(!calib.add(301.0f, medBoxH));                       // Window [360, 302, 301] -> spread 19%
    assert(calib.add(300.0f, medBoxH));                        // Window [302, 301, 300] -> spread < 1%
    assert(std::fabs(medBoxH - 301.0f) < .001f);
    calib.reset();
    assert(calib.count() == 0);

    // analyzeFraming(): margin=8px on 320x240 QVGA, minHeightRatio=0.20.
    assert(analyzeFraming({60, 20, 140, 210, .9f}, 320, 240) == FramingStatus::FullBody);
    assert(analyzeFraming({60, 8, 140, 210, .9f}, 320, 240) == FramingStatus::TopClipped);
    assert(analyzeFraming({60, 20, 140, 232, .9f}, 320, 240) == FramingStatus::BottomClipped);
    assert(analyzeFraming({60, 5, 140, 235, .9f}, 320, 240) == FramingStatus::TooClose);
    assert(analyzeFraming({8, 20, 140, 210, .9f}, 320, 240) == FramingStatus::SideClipped);
    assert(analyzeFraming({60, 20, 315, 210, .9f}, 320, 240) == FramingStatus::SideClipped);
    // Priority: TooClose > Top/Bottom > Side; small edge peek stays clipped, unclipped tiny box -> None.
    assert(analyzeFraming({2, 2, 318, 238, .9f}, 320, 240) == FramingStatus::TooClose);
    assert(analyzeFraming({2, 4, 140, 210, .9f}, 320, 240) == FramingStatus::TopClipped);
    assert(analyzeFraming({2, 20, 140, 236, .9f}, 320, 240) == FramingStatus::BottomClipped);
    assert(analyzeFraming({100, 2, 160, 30, .9f}, 320, 240) == FramingStatus::TopClipped);
    assert(analyzeFraming({100, 100, 150, 140, .9f}, 320, 240) == FramingStatus::None); // height 40 < 48 (20%)
    assert(analyzeFraming({100, 100, 101, 180, .9f}, 320, 240) == FramingStatus::None); // width < 2 px
    assert(std::string(framingStatusName(FramingStatus::TooClose)) == "TOO_CLOSE");

    // groundHeightAtServo(): recover exact physical height & distance across multiple tilt angles.
    const int w = 320, h = 240;
    const float focal = 320.0f, camH = 105.0f, trueH = 170.0f, trueZ = 3.50f;
    const float cy = (h - 1) / 2.0f;
    const float feetWorldAngle = std::atan2(-(camH / 100.0f), trueZ);
    const float headWorldAngle = std::atan2((trueH - camH) / 100.0f, trueZ);
    struct ServoCase { float servoDeg; float levelDeg; int8_t dir; float offsetDeg; float expectedTilt; };
    const ServoCase cases[] = {
        {85.0f, 90.0f, -1, 0.0f, 5.0f},   // +5 deg tilt down
        {90.0f, 90.0f, -1, 0.0f, 0.0f},   // level
        {92.0f, 90.0f, -1, 0.0f, -2.0f},  // -2 deg tilt up
        {95.0f, 90.0f, 1, 0.0f, 5.0f},    // reversed servo direction (+1)
        {89.0f, 90.0f, -1, 1.0f, 2.0f},   // non-zero optical offset
    };
    for (const auto& sc : cases) {
        const float thetaRad = sc.expectedTilt * 3.14159265359f / 180.0f;
        const float y2 = cy - focal * std::tan(feetWorldAngle + thetaRad);
        const float y1 = cy - focal * std::tan(headWorldAngle + thetaRad);
        const PersonBox pbox{130.0f, y1, 189.0f, y2, 0.92f};
        assert(analyzeFraming(pbox, w, h) == FramingStatus::FullBody);
        float outTilt = 0.0f;
        assert(groundHeightAtServo(pbox, w, h, focal, camH, sc.servoDeg,
                                   sc.levelDeg, sc.dir, sc.offsetDeg, cm, distance, outTilt));
        assert(std::fabs(outTilt - sc.expectedTilt) < 0.001f);
        assert(std::fabs(cm - trueH) < 0.05f);
        assert(std::fabs(distance - trueZ) < 0.005f);
    }
}
