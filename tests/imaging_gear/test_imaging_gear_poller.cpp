// Imaging-gear poller tests (open-astro/openastro-phd2#33,
// open-astro/openastro-guider#82): what is read from which device, the
// imaging-camera choice, INFO lines from changes between polls, and that
// the cache never waits on a slow server.

#include <gtest/gtest.h>

#include "imaging_gear_poller.h"

#include <atomic>
#include <map>

using namespace ImagingGear;
using namespace std::chrono_literals;

namespace
{
struct FakeServer
{
    std::mutex mutex;
    bool reachable = true;
    std::vector<ConfiguredDevice> devices;
    std::map<std::string, std::string> strings;
    std::map<std::string, double> doubles;
    std::map<std::string, int> ints;
    std::map<std::string, bool> bools;
    std::map<std::string, std::vector<std::string>> stringLists;
    std::map<std::string, std::vector<int>> intLists;
    std::atomic<int> delayMs { 0 };
    std::atomic<int> calls { 0 };
};

class FakeReader : public Reader
{
    FakeServer& m_s;

    template<typename M, typename T>
    bool Lookup(M& map, const std::string& path, T *value)
    {
        ++m_s.calls;
        std::this_thread::sleep_for(std::chrono::milliseconds(m_s.delayMs.load()));
        std::lock_guard<std::mutex> lock(m_s.mutex);
        auto it = map.find(path);
        if (!m_s.reachable || it == map.end())
            return false;
        *value = it->second;
        return true;
    }

public:
    explicit FakeReader(FakeServer& s) : m_s(s) { }
    bool ConfiguredDevices(std::vector<ConfiguredDevice> *d) override
    {
        ++m_s.calls;
        std::this_thread::sleep_for(std::chrono::milliseconds(m_s.delayMs.load()));
        std::lock_guard<std::mutex> lock(m_s.mutex);
        *d = m_s.devices;
        return m_s.reachable;
    }
    bool ServerName(std::string *name) override
    {
        *name = "AlpacaBridge";
        return true;
    }
    bool GetString(const std::string& p, std::string *v) override { return Lookup(m_s.strings, p, v); }
    bool GetDouble(const std::string& p, double *v) override { return Lookup(m_s.doubles, p, v); }
    bool GetInt(const std::string& p, int *v) override { return Lookup(m_s.ints, p, v); }
    bool GetBool(const std::string& p, bool *v) override { return Lookup(m_s.bools, p, v); }
    bool GetStringList(const std::string& p, std::vector<std::string> *v) override { return Lookup(m_s.stringLists, p, v); }
    bool GetIntList(const std::string& p, std::vector<int> *v) override { return Lookup(m_s.intLists, p, v); }
};

// guide camera 0, imaging camera 1, telescope 0, filter wheel, focuser, conditions
void Rig(FakeServer& s)
{
    s.devices = { { "Telescope", 0, "EQ6" },   { "Camera", 0, "ASI120MM" }, { "Camera", 1, "ASI2600MM" },
                  { "FilterWheel", 0, "EFW" }, { "Focuser", 0, "EAF" },     { "ObservingConditions", 0, "WeeWX" } };
    s.strings["telescope/0/name"] = "EQ6-R Pro";
    s.doubles["telescope/0/focallength"] = 0.25;
    s.doubles["telescope/0/aperturediameter"] = 0.051;
    s.strings["camera/1/name"] = "ZWO ASI2600MM Pro";
    s.strings["camera/1/sensorname"] = "IMX571";
    s.ints["camera/1/sensortype"] = 0;
    s.doubles["camera/1/pixelsizex"] = 3.76;
    s.doubles["camera/1/pixelsizey"] = 3.76;
    s.ints["camera/1/cameraxsize"] = 6248;
    s.ints["camera/1/cameraysize"] = 4176;
    s.ints["camera/1/binx"] = 1;
    s.ints["camera/1/biny"] = 1;
    s.ints["camera/1/gain"] = 100;
    s.ints["camera/1/offset"] = 50;
    s.ints["camera/1/readoutmode"] = 1;
    s.stringLists["camera/1/readoutmodes"] = { "Normal", "High Gain" };
    s.bools["camera/1/cooleron"] = true;
    s.doubles["camera/1/ccdtemperature"] = -10.0;
    s.ints["camera/1/camerastate"] = 0;
    s.strings["filterwheel/0/name"] = "ZWO EFW";
    s.stringLists["filterwheel/0/names"] = { "L", "R", "Ha", "OIII" };
    s.intLists["filterwheel/0/focusoffsets"] = { 0, 5, 0, 12 };
    s.ints["filterwheel/0/position"] = 2;
    s.strings["focuser/0/name"] = "ZWO EAF";
    s.ints["focuser/0/position"] = 12345;
    s.doubles["focuser/0/temperature"] = 8.4;
    s.strings["observingconditions/0/name"] = "WeeWX";
    s.doubles["observingconditions/0/windspeed"] = 3.2;
    s.doubles["observingconditions/0/temperature"] = 8.9;
}

Context AlpacaContext()
{
    Context c;
    c.alpaca = true;
    c.host = "192.168.1.20";
    c.port = 11111;
    c.guideCamera = 0;
    c.telescope = 0;
    return c;
}

Poller::ReaderFactory FactoryFor(FakeServer& s)
{
    return [&s](const std::string&, long) { return std::unique_ptr<Reader>(new FakeReader(s)); };
}

Poller::Timing Instant()
{
    Poller::Timing t;
    t.fast = 0ms;
    t.full = 1h;
    t.conditions = 1h;
    t.retry = 0ms;
    return t;
}
}

TEST(ImagingGearPoller, ReadsTheRig)
{
    FakeServer s;
    Rig(s);
    Poller p(FactoryFor(s), Instant(), false);
    p.SetContext(AlpacaContext());
    p.PollOnce();

    Snapshot snap = p.Latest();
    EXPECT_EQ(snap.source, Source::Alpaca);
    EXPECT_EQ(snap.server, "AlpacaBridge 192.168.1.20:11111");
    ASSERT_TRUE(snap.telescope);
    EXPECT_EQ(snap.telescope->name, "EQ6-R Pro");
    EXPECT_DOUBLE_EQ(*snap.telescope->focalLengthM, 0.25);
    ASSERT_TRUE(snap.camera);
    EXPECT_EQ(snap.camera->name, "ZWO ASI2600MM Pro");
    EXPECT_EQ(snap.camera->readoutMode, std::string("High Gain"));
    ASSERT_TRUE(snap.filterWheel);
    EXPECT_EQ(snap.filterWheel->position, 2);
    ASSERT_TRUE(snap.focuser);
    ASSERT_TRUE(snap.conditions);
    EXPECT_FALSE(snap.conditions->starFwhm.has_value()); // not reported
    EXPECT_FALSE(snap.rotator.has_value()); // none configured
    EXPECT_NEAR(*PixelScale(snap), 3.10, 0.005);
}

TEST(ImagingGearPoller, NotAlpacaAndUnreachable)
{
    FakeServer s;
    Rig(s);
    Poller p(FactoryFor(s), Instant(), false);
    p.SetContext(Context());
    p.PollOnce();
    EXPECT_EQ(p.Latest().source, Source::NotAlpaca);
    EXPECT_EQ(s.calls, 0);

    s.reachable = false;
    p.SetContext(AlpacaContext());
    p.PollOnce();
    EXPECT_EQ(p.Latest().source, Source::Unavailable);
}

TEST(ImagingGearPoller, PicksTheConnectedExposingCamera)
{
    FakeServer s;
    Rig(s);
    s.devices.push_back({ "Camera", 2, "Spare" });
    s.bools["camera/1/connected"] = true;
    s.ints["camera/1/camerastate"] = 2;
    s.bools["camera/2/connected"] = false;
    Poller p(FactoryFor(s), Instant(), false);
    p.SetContext(AlpacaContext());
    p.PollOnce();
    ASSERT_TRUE(p.Latest().camera);
    EXPECT_EQ(p.Latest().camera->name, "ZWO ASI2600MM Pro");
}

TEST(ImagingGearPoller, SeveralIdleConnectedCamerasAreAmbiguous)
{
    FakeServer s;
    Rig(s);
    s.devices.push_back({ "Camera", 2, "Spare" });
    s.bools["camera/1/connected"] = true;
    s.bools["camera/2/connected"] = true;
    s.ints["camera/2/camerastate"] = 0;
    Poller p(FactoryFor(s), Instant(), false);
    p.SetContext(AlpacaContext());
    p.PollOnce();
    EXPECT_FALSE(p.Latest().camera.has_value());
    EXPECT_EQ(p.Latest().ambiguousCameras, 2);
}

TEST(ImagingGearPoller, ChangesBetweenPollsBecomeInfoLines)
{
    FakeServer s;
    Rig(s);
    Poller p(FactoryFor(s), Instant(), false);
    p.SetContext(AlpacaContext());
    p.PollOnce();
    EXPECT_TRUE(p.TakeInfoLines().empty());

    s.ints["filterwheel/0/position"] = 3;
    s.ints["camera/1/camerastate"] = 2;
    p.PollOnce();
    std::vector<std::string> lines = p.TakeInfoLines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "INFO: EQUIPMENT Filter = OIII, Position = 4 of 4, Focus offset = 12");
    EXPECT_EQ(lines[1], "INFO: IMAGING EXPOSURE start, Duration = unknown, Filter = OIII");

    s.ints["camera/1/camerastate"] = 3; // reading out
    s.doubles["camera/1/lastexposureduration"] = 300.0;
    p.PollOnce();
    lines = p.TakeInfoLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "INFO: IMAGING EXPOSURE end, Duration = 300.0 s");
    EXPECT_TRUE(p.TakeInfoLines().empty());
}

TEST(ImagingGearPoller, ConditionsLineOncePerInterval)
{
    FakeServer s;
    Rig(s);
    Poller::Timing t = Instant();
    t.conditions = 0ms;
    Poller p(FactoryFor(s), t, false);
    p.SetContext(AlpacaContext());
    p.PollOnce();
    std::vector<std::string> lines = p.TakeInfoLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].rfind("INFO: CONDITIONS Seeing = unknown, Wind speed = 3.2 m/s", 0), 0u) << lines[0];
}

TEST(ImagingGearPoller, CacheNeverWaitsForASlowServer)
{
    FakeServer s;
    Rig(s);
    s.delayMs = 5000; // every request takes 5 s
    Poller::Timing t;
    Poller p(FactoryFor(s), t, true);
    p.SetContext(AlpacaContext());
    std::this_thread::sleep_for(50ms); // the poll thread is now inside a 5 s request

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; i++)
    {
        p.SetContext(AlpacaContext());
        (void) p.Latest();
        (void) p.TakeInfoLines();
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, 100ms);
    s.delayMs = 0;
}

TEST(ImagingGearPoller, StoppingDoesNotFinishASlowPollCycle)
{
    FakeServer s;
    Rig(s);
    s.delayMs = 300; // a full cycle is ~40 reads: 12 s
    auto p = std::make_unique<Poller>(FactoryFor(s), Poller::Timing(), true);
    p->SetContext(AlpacaContext());
    std::this_thread::sleep_for(50ms);

    auto start = std::chrono::steady_clock::now();
    p.reset(); // waits for the read in flight only
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1000ms);
}
