#pragma once
#include <cstdio>
#include <cstring>
#include "measurement_state.h"

inline bool decodeCameraLabel(const char* label, PersonStatus& status, float& height, float& distance) {
    height = distance = 0;
    if (!std::strcmp(label, "NO_PEOPLE")) status = PersonStatus::None;
    else if (!std::strcmp(label, "PERSON_UNCALIBRATED")) status = PersonStatus::NeedsCalibration;
    else if (!std::strcmp(label, "PERSON_PARTIAL")) status = PersonStatus::Partial;
    else if (!std::strcmp(label, "MULTIPLE_PEOPLE")) status = PersonStatus::Multiple;
    else {
        int consumed = 0;
        if (std::sscanf(label, "MEAS_H%f_D%f%n", &height, &distance, &consumed) != 2 ||
            !consumed || label[consumed] != '\0' || !std::isfinite(height) ||
            !std::isfinite(distance) || height < 30 || height > 250 || distance <= 0 || distance > 20) return false;
        status = PersonStatus::Measured;
    }
    return true;
}
