// Alpaca camera gain tests (open-astro/openastro-guider#79).
//
// CameraAlpaca reads GainMin/GainMax on connect and turns PHD2's 0-100 %
// gain setting into the driver's raw gain before each exposure. ZWO drivers
// (and AlpacaBridge) use ASCOM "value mode": GainMin/GainMax are valid and
// Gains throws NotImplemented. The decision and the mapping live in
// alpaca_gain.cpp so they can be tested without a camera or a server.

#include <gtest/gtest.h>

#include "alpaca_gain.h"

TEST(AlpacaGain, ValueModeRangeIsSupported)
{
    AlpacaGain::Range r = AlpacaGain::FromProbe(true, 0, true, 100);
    EXPECT_TRUE(r.supported);
    EXPECT_EQ(r.min, 0);
    EXPECT_EQ(r.max, 100);
}

TEST(AlpacaGain, MissingMinOrMaxIsUnsupported)
{
    EXPECT_FALSE(AlpacaGain::FromProbe(false, 0, true, 100).supported);
    EXPECT_FALSE(AlpacaGain::FromProbe(true, 0, false, 0).supported);
}

TEST(AlpacaGain, EmptyOrInvertedRangeIsUnsupported)
{
    // A server that answers every unknown GET with 0 reports 0..0.
    EXPECT_FALSE(AlpacaGain::FromProbe(true, 0, true, 0).supported);
    EXPECT_FALSE(AlpacaGain::FromProbe(true, 50, true, 10).supported);
}

TEST(AlpacaGain, PercentMapsOntoTheDriverRange)
{
    AlpacaGain::Range asi120 = AlpacaGain::FromProbe(true, 0, true, 100);
    EXPECT_EQ(AlpacaGain::PercentToRaw(0, asi120), 0);
    EXPECT_EQ(AlpacaGain::PercentToRaw(95, asi120), 95);
    EXPECT_EQ(AlpacaGain::PercentToRaw(100, asi120), 100);

    AlpacaGain::Range asi290 = AlpacaGain::FromProbe(true, 0, true, 570);
    EXPECT_EQ(AlpacaGain::PercentToRaw(50, asi290), 285);
    EXPECT_EQ(AlpacaGain::PercentToRaw(33, asi290), 188); // 188.1 rounds

    AlpacaGain::Range offset = AlpacaGain::FromProbe(true, 10, true, 20);
    EXPECT_EQ(AlpacaGain::PercentToRaw(0, offset), 10);
    EXPECT_EQ(AlpacaGain::PercentToRaw(25, offset), 13); // 12.5 rounds up
    EXPECT_EQ(AlpacaGain::PercentToRaw(100, offset), 20);
}

TEST(AlpacaGain, PercentOutsideZeroToHundredIsClamped)
{
    AlpacaGain::Range r = AlpacaGain::FromProbe(true, 0, true, 300);
    EXPECT_EQ(AlpacaGain::PercentToRaw(-5, r), 0);
    EXPECT_EQ(AlpacaGain::PercentToRaw(150, r), 300);
}
