// The Android entry point's pure decisions (AndroidLogic.h): density from the configuration's dpi, safe-area insets
// from the system bars and the display cutout, and the thermal status ladder.
#include "TestUtil.h"
#include "platform/android/AndroidLogic.h"

using namespace cs;
using namespace cs::android;
using namespace test;

int main() {
    check(densityFromDpi(160) == 1.f && densityFromDpi(480) == 3.f && densityFromDpi(420) == 2.625f, "density: dpi / 160 (mdpi = 1)");
    check(densityFromDpi(0) == 1.f && densityFromDpi(-5) == 1.f, "unknown density (0): 1, never a division by zero");
    const Insets a = insetsInPoints(72, 48, 0, 0, 3.f);
    check(a.top == 24.f && a.bottom == 16.f, "insets: pixels -> points");
    const Insets b = insetsInPoints(72, 48, 120, 0, 3.f);
    check(b.top == 40.f && b.bottom == 16.f, "a camera cutout taller than the status bar wins at the top");
    const Insets c = insetsInPoints(0, 0, 0, 90, 3.f);
    check(c.top == 0.f && c.bottom == 30.f, "...and a bottom cutout at the bottom");
    const Insets d = insetsInPoints(30, 30, 0, 0, 0.f);
    check(d.top == 30.f && d.bottom == 30.f, "no density: treated as 1");
    check(thermalFromStatus(0) == quality::Thermal::Nominal && thermalFromStatus(1) == quality::Thermal::Nominal,
          "thermal: none / light -> nominal");
    check(thermalFromStatus(2) == quality::Thermal::Fair && thermalFromStatus(3) == quality::Thermal::Serious, "moderate -> fair, severe -> serious");
    check(thermalFromStatus(4) == quality::Thermal::Critical && thermalFromStatus(6) == quality::Thermal::Critical,
          "critical, emergency, shutdown -> critical");
    check(thermalFromStatus(-1) == quality::Thermal::Nominal, "an unknown status (-1, API < 29): nominal");
    return finish("AndroidLogic");
}
