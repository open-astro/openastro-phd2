/*
 *  alpaca_gain.h
 *  PHD Guiding
 *
 *  Gain support for Alpaca cameras: decide from GainMin/GainMax whether the
 *  driver has a usable gain range, and map PHD2's 0-100 % gain onto it.
 *  Kept free of wx so it can be unit tested (tests/camera/test_alpaca_gain.cpp).
 */

#ifndef ALPACA_GAIN_H_INCLUDED
#define ALPACA_GAIN_H_INCLUDED

namespace AlpacaGain
{
struct Range
{
    bool supported;
    int min;
    int max;
};

// minOk/maxOk say whether the GainMin/GainMax reads succeeded
Range FromProbe(bool minOk, int min, bool maxOk, int max);

// pct is PHD2's gain setting (0-100); the result is the driver's raw gain
int PercentToRaw(int pct, const Range& range);
}

#endif
