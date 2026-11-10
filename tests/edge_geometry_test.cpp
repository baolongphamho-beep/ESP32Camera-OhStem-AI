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
}
