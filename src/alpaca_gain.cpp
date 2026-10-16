/*
 *  alpaca_gain.cpp
 *  PHD Guiding
 *
 *  See alpaca_gain.h.
 */

#include "alpaca_gain.h"

#include <algorithm>
#include <cmath>

namespace AlpacaGain
{
Range FromProbe(bool minOk, int min, bool maxOk, int max)
{
    // ASCOM value mode: GainMin/GainMax both readable and spanning a range.
    // Index mode (a Gains name list) is not supported yet.
    Range r;
    r.supported = minOk && maxOk && max > min;
    r.min = min;
    r.max = max;
    return r;
}

int PercentToRaw(int pct, const Range& range)
{
    pct = std::clamp(pct, 0, 100);
    return range.min + (int) std::lround(pct * (range.max - range.min) / 100.0);
}
}
