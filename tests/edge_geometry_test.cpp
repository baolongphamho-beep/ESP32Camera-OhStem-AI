#include "../firmware/edge_yolo/main/edge_geometry.h"
#include <cassert>

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
}
