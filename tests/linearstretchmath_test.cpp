#include "../linearstretchmath.h"

#include <QCoreApplication>
#include <QDebug>
#include <cmath>

namespace {

bool nearlyEqual(double lhs, double rhs, double tolerance = 1e-6)
{
    return std::abs(lhs - rhs) <= tolerance;
}

bool check(bool condition, const char *message)
{
    if (!condition)
        qCritical() << "FAIL:" << message;
    return condition;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    ok &= check(LinearStretchMath::usbWordToDn13(0) == 0,
                "zero USB word");
    ok &= check(LinearStretchMath::usbWordToDn13(8000) == 1000,
                "USB word to 13-bit conversion");
    ok &= check(LinearStretchMath::usbWordToDn13(65535) == 8191,
                "16-bit saturation conversion");

    const LinearStretchMath::Parameters base =
        LinearStretchMath::calculate(2000.0, 7000.0, 0.0, 8191.0);
    ok &= check(base.valid, "valid bright/dark span");
    ok &= check(nearlyEqual(base.k, 8191.0 / 5000.0), "base K");
    ok &= check(nearlyEqual(base.b, -base.k * 2000.0), "base B");
    ok &= check(LinearStretchMath::apply13(2000, base) == 0,
                "dark maps to target black");
    ok &= check(LinearStretchMath::apply13(7000, base) == 8191,
                "bright maps to target white");

    const LinearStretchMath::Parameters adjusted =
        LinearStretchMath::adjusted(base, 1.1, 100.0);
    ok &= check(adjusted.valid, "adjusted parameters valid");
    ok &= check(nearlyEqual(adjusted.k, base.k * 1.1), "K ratio");
    ok &= check(nearlyEqual(adjusted.b, base.b * 1.1 + 100.0),
                "B additive offset");
    ok &= check(LinearStretchMath::apply13(2000, adjusted) == 100,
                "B offset raises mapped dark level");

    ok &= check(!LinearStretchMath::calculate(7000.0, 2000.0, 0.0, 8191.0).valid,
                "invalid reversed bright/dark span");

    if (ok)
        qInfo() << "PASS linearstretchmath_test";
    return ok ? 0 : 1;
}
