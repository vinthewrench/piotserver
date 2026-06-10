/*
 * adapted from: http://www.voidware.com/phase.c
 * CalculateJulianDate from: https://www.programmingassignmenthelper.com/convert-a-date-to-the-julian-day-number-in-c/
 */

#include "lunar.hpp"

#include <cmath>
#include <ctime>

int Lunar::CalculateJulianDay() {
    const int BASE_YEAR = 1900;
    time_t ttime = time(nullptr);
    tm *local_time = localtime(&ttime);

    return CalculateJulianDay(
        local_time->tm_year + BASE_YEAR,
        local_time->tm_mon + 1,
        local_time->tm_mday
    );
}

int Lunar::CalculateJulianDay(int year, int month, double day) {
    int a, m, y, leap_days;

    a = ((14 - month) / 12);
    m = (month - 3) + (12 * a);
    y = year + 4800 - a;
    leap_days = (y / 4) - (y / 100) + (y / 400);

    return static_cast<int>(
        day
        + (((153 * m) + 2.0) / 5.0)
        + (365 * y)
        + leap_days
        - 32045
    );
}

Phase Lunar::CalculateMoonPhase() {
    auto julianDay = CalculateJulianDay();
    return CalculateMoonPhase(julianDay);
}

Phase Lunar::CalculateMoonPhase(int julianDay) {
    auto illumination = CalculateIllumination(julianDay);

    return Phase {
        .julianDay = julianDay,
        .phase = illumination.phase,
        .segment = CalculateSegment(illumination.phase, illumination.visible),
        .visible = illumination.visible,
    };
}

Phase Lunar::CalculateMoonPhase(int year, int month, double day) {
    auto julianDay = CalculateJulianDay(year, month, day);
    return CalculateMoonPhase(julianDay);
}

std::string Lunar::GetSegmentName(int segment) {
    const std::array<std::string, 8> segmentNames = {
        "New",
        "Waxing Crescent",
        "First Quarter",
        "Waxing Gibbous",
        "Full",
        "Waning Gibbous",
        "Third Quarter",
        "Waning Crescent"
    };

    return segmentNames.at(segment);
}

Segment Lunar::CalculateSegment(double phase, double visible) {
    /*
     * phase:
     *   0.00 = New
     *   0.25 = First Quarter
     *   0.50 = Full
     *   0.75 = Third Quarter / Last Quarter
     *
     * visible:
     *   illuminated fraction, 0.0 to 1.0
     *
     * The old code used:
     *
     *   (Segment)(int)(phase * 8)
     *
     * That is wrong for user-facing names.
     *
     * Example:
     *   phase around 0.78 and visible around 0.26 would become:
     *
     *       Third Quarter
     *
     *   But the visible shape is clearly:
     *
     *       Waning Crescent
     *
     * User-facing moon labels should describe the visible shape:
     *
     *   visible below 50% and waxing  = Waxing Crescent
     *   visible near  50% and waxing  = First Quarter
     *   visible above 50% and waxing  = Waxing Gibbous
     *   visible above 50% and waning  = Waning Gibbous
     *   visible near  50% and waning  = Third Quarter
     *   visible below 50% and waning  = Waning Crescent
     */

    phase = phase - std::floor(phase);

    if (visible < 0.0) {
        visible = 0.0;
    }
    else if (visible > 1.0) {
        visible = 1.0;
    }

    constexpr double NEW_FULL_EPSILON = 0.02;
    constexpr double QUARTER_EPSILON = 0.035;

    const bool waxing = phase < 0.5;

    if (visible <= NEW_FULL_EPSILON) {
        return New;
    }

    if (visible >= 1.0 - NEW_FULL_EPSILON) {
        return Full;
    }

    if (std::fabs(visible - 0.5) <= QUARTER_EPSILON) {
        return waxing ? FirstQuarter : ThirdQuarter;
    }

    if (waxing) {
        return visible < 0.5 ? WaxingCrescent : WaxingGibbous;
    }

    return visible < 0.5 ? WaningCrescent : WaningGibbous;
}

Position Lunar::CalculateSolarCoordinates(int julianDay) {
    auto solarMeanAnomaly = RAD * (357.5291 + 0.98560028 * julianDay);
    auto eclipticLongitude = CalculateEclipticLongitude(solarMeanAnomaly);

    return Position {
        .declination = CalculateDeclination(eclipticLongitude, 0),
        .distance = 0.0,
        .rightAscension = CalculateRightAscension(eclipticLongitude, 0)
    };
}

Position Lunar::CalculateLunarCoordinates(int julianDay) {
    auto eclipticLongitude = RAD * (218.316 + 13.176396 * julianDay);
    auto meanAnomaly = RAD * (134.963 + 13.064993 * julianDay);
    auto meanDistance = RAD * (93.272 + 13.229350 * julianDay);

    auto longitude = eclipticLongitude + RAD * 6.289 * std::sin(meanAnomaly);
    auto latitude = RAD * 5.128 * std::sin(meanDistance);
    auto distance = 385001 - 20905 * std::cos(meanAnomaly);

    return Position {
        .declination = CalculateDeclination(longitude, latitude),
        .distance = distance,
        .rightAscension = CalculateRightAscension(longitude, latitude)
    };
}

Illumination Lunar::CalculateIllumination(int julianDay) {
    auto day = julianDay - MILLENNIUM_EPOCH;

    auto solarCoordinates = CalculateSolarCoordinates(day);
    auto lunarCoordinates = CalculateLunarCoordinates(day);

    auto phi = std::acos(
        std::sin(solarCoordinates.declination)
        * std::sin(lunarCoordinates.declination)
        + std::cos(solarCoordinates.declination)
        * std::cos(lunarCoordinates.declination)
        * std::cos(solarCoordinates.rightAscension - lunarCoordinates.rightAscension)
    );

    auto inc = std::atan2(
        DISTANCE_FROM_EARTH_TO_SUN * std::sin(phi),
        lunarCoordinates.distance - DISTANCE_FROM_EARTH_TO_SUN * std::cos(phi)
    );

    auto angle = std::atan2(
        std::cos(solarCoordinates.declination)
        * std::sin(solarCoordinates.rightAscension - lunarCoordinates.rightAscension),

        std::sin(solarCoordinates.declination)
        * std::cos(lunarCoordinates.declination)
        - std::cos(solarCoordinates.declination)
        * std::sin(lunarCoordinates.declination)
        * std::cos(solarCoordinates.rightAscension - lunarCoordinates.rightAscension)
    );

    auto phase = 0.5 + 0.5 * inc * (angle < 0 ? -1 : 1) / PI;

    phase = phase - std::floor(phase);

    return Illumination {
        .angle = angle,
        .phase = phase,
        .visible = (1 + std::cos(inc)) / 2
    };
}
