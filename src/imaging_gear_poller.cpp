/*
 *  imaging_gear_poller.cpp
 *  PHD Guiding
 *
 *  See imaging_gear_poller.h.
 */

#include "imaging_gear_poller.h"

#include <cstdio>

namespace ImagingGear
{
namespace
{
    const size_t MAX_QUEUED_LINES = 200;

    // Fails every read once stopping, so shutdown waits for the read in
    // flight only, not the rest of the poll cycle.
    class StoppableReader : public Reader
    {
        Reader& m_r;
        const std::atomic<bool>& m_stopping;

    public:
        StoppableReader(Reader& r, const std::atomic<bool>& stopping) : m_r(r), m_stopping(stopping) { }
        bool ConfiguredDevices(std::vector<ConfiguredDevice> *d) override { return !m_stopping && m_r.ConfiguredDevices(d); }
        bool ServerName(std::string *n) override { return !m_stopping && m_r.ServerName(n); }
        bool GetString(const std::string& p, std::string *v) override { return !m_stopping && m_r.GetString(p, v); }
        bool GetDouble(const std::string& p, double *v) override { return !m_stopping && m_r.GetDouble(p, v); }
        bool GetInt(const std::string& p, int *v) override { return !m_stopping && m_r.GetInt(p, v); }
        bool GetBool(const std::string& p, bool *v) override { return !m_stopping && m_r.GetBool(p, v); }
        bool GetStringList(const std::string& p, std::vector<std::string> *v) override
        {
            return !m_stopping && m_r.GetStringList(p, v);
        }
        bool GetIntList(const std::string& p, std::vector<int> *v) override { return !m_stopping && m_r.GetIntList(p, v); }
    };

    std::string Path(const char *type, int number, const char *property)
    {
        return std::string(type) + "/" + std::to_string(number) + "/" + property;
    }

    template<typename T, typename Get>
    std::optional<T> Read(Get get)
    {
        T value { };
        if (get(&value))
            return value;
        return std::nullopt;
    }

    std::optional<std::string> ReadString(Reader& r, const std::string& path)
    {
        return Read<std::string>([&](std::string *v) { return r.GetString(path, v); });
    }
    std::optional<double> ReadDouble(Reader& r, const std::string& path)
    {
        return Read<double>([&](double *v) { return r.GetDouble(path, v); });
    }
    std::optional<int> ReadInt(Reader& r, const std::string& path)
    {
        return Read<int>([&](int *v) { return r.GetInt(path, v); });
    }
    std::optional<bool> ReadBool(Reader& r, const std::string& path)
    {
        return Read<bool>([&](bool *v) { return r.GetBool(path, v); });
    }

    std::string Name(Reader& r, const char *type, int number, const std::string& fallback)
    {
        std::optional<std::string> name = ReadString(r, Path(type, number, "name"));
        return name && !name->empty() ? *name : fallback;
    }

    bool Exposing(const std::optional<int>& cameraState)
    {
        // ASCOM CameraState: 1 waiting, 2 exposing
        return cameraState && (*cameraState == 1 || *cameraState == 2);
    }

    // every camera except the guide camera; with several, the one that is
    // connected and exposing, else the only connected one
    std::optional<int> ChooseCamera(Reader& r, const std::vector<int>& candidates, int *ambiguous)
    {
        *ambiguous = 0;
        if (candidates.size() == 1)
            return candidates[0];
        std::vector<int> connected, exposing;
        for (int n : candidates)
        {
            if (ReadBool(r, Path("camera", n, "connected")).value_or(false))
            {
                connected.push_back(n);
                if (Exposing(ReadInt(r, Path("camera", n, "camerastate"))))
                    exposing.push_back(n);
            }
        }
        if (exposing.size() == 1)
            return exposing[0];
        if (connected.size() == 1)
            return connected[0];
        *ambiguous = (int) candidates.size();
        return std::nullopt;
    }

    Telescope ReadTelescope(Reader& r, int n)
    {
        Telescope t;
        t.name = Name(r, "telescope", n, "unknown");
        t.focalLengthM = ReadDouble(r, Path("telescope", n, "focallength"));
        t.apertureM = ReadDouble(r, Path("telescope", n, "aperturediameter"));
        return t;
    }

    Camera ReadCamera(Reader& r, int n)
    {
        Camera c;
        c.name = Name(r, "camera", n, "unknown");
        c.sensorName = ReadString(r, Path("camera", n, "sensorname"));
        c.sensorType = ReadInt(r, Path("camera", n, "sensortype"));
        c.pixelSizeX = ReadDouble(r, Path("camera", n, "pixelsizex"));
        c.pixelSizeY = ReadDouble(r, Path("camera", n, "pixelsizey"));
        c.sizeX = ReadInt(r, Path("camera", n, "cameraxsize"));
        c.sizeY = ReadInt(r, Path("camera", n, "cameraysize"));
        c.binX = ReadInt(r, Path("camera", n, "binx"));
        c.binY = ReadInt(r, Path("camera", n, "biny"));
        c.gain = ReadInt(r, Path("camera", n, "gain"));
        c.offset = ReadInt(r, Path("camera", n, "offset"));
        std::optional<int> mode = ReadInt(r, Path("camera", n, "readoutmode"));
        std::vector<std::string> modes;
        if (mode && r.GetStringList(Path("camera", n, "readoutmodes"), &modes) && *mode >= 0 && *mode < (int) modes.size())
            c.readoutMode = modes[*mode];
        c.coolerOn = ReadBool(r, Path("camera", n, "cooleron"));
        c.sensorTemp = ReadDouble(r, Path("camera", n, "ccdtemperature"));
        return c;
    }

    void ReadFilterPosition(Reader& r, int n, FilterWheel *fw)
    {
        std::optional<int> pos = ReadInt(r, Path("filterwheel", n, "position"));
        fw->position = pos && *pos >= 0 ? pos : std::nullopt; // -1 while moving
    }

    FilterWheel ReadFilterWheel(Reader& r, int n)
    {
        FilterWheel fw;
        fw.name = Name(r, "filterwheel", n, "unknown");
        r.GetStringList(Path("filterwheel", n, "names"), &fw.names);
        r.GetIntList(Path("filterwheel", n, "focusoffsets"), &fw.focusOffsets);
        ReadFilterPosition(r, n, &fw);
        return fw;
    }

    void ReadRotatorPosition(Reader& r, int n, Rotator *rot)
    {
        rot->position = ReadDouble(r, Path("rotator", n, "position"));
        rot->mechanicalPosition = ReadDouble(r, Path("rotator", n, "mechanicalposition"));
    }

    Rotator ReadRotator(Reader& r, int n)
    {
        Rotator rot;
        rot.name = Name(r, "rotator", n, "unknown");
        rot.reverse = ReadBool(r, Path("rotator", n, "reverse"));
        ReadRotatorPosition(r, n, &rot);
        return rot;
    }

    void ReadFocuserPosition(Reader& r, int n, Focuser *f)
    {
        f->position = ReadInt(r, Path("focuser", n, "position"));
        f->temperature = ReadDouble(r, Path("focuser", n, "temperature"));
    }

    Focuser ReadFocuser(Reader& r, int n)
    {
        Focuser f;
        f.name = Name(r, "focuser", n, "unknown");
        ReadFocuserPosition(r, n, &f);
        return f;
    }

    Conditions ReadConditions(Reader& r, int n)
    {
        Conditions oc;
        oc.name = Name(r, "observingconditions", n, "unknown");
        oc.starFwhm = ReadDouble(r, Path("observingconditions", n, "starfwhm"));
        oc.windSpeed = ReadDouble(r, Path("observingconditions", n, "windspeed"));
        oc.windGust = ReadDouble(r, Path("observingconditions", n, "windgust"));
        oc.temperature = ReadDouble(r, Path("observingconditions", n, "temperature"));
        oc.humidity = ReadDouble(r, Path("observingconditions", n, "humidity"));
        oc.dewPoint = ReadDouble(r, Path("observingconditions", n, "dewpoint"));
        oc.pressure = ReadDouble(r, Path("observingconditions", n, "pressure"));
        oc.cloudCover = ReadDouble(r, Path("observingconditions", n, "cloudcover"));
        oc.skyQuality = ReadDouble(r, Path("observingconditions", n, "skyquality"));
        return oc;
    }

    std::string FilterSuffix(const Snapshot& s)
    {
        if (!s.filterWheel)
            return "";
        const FilterWheel& fw = *s.filterWheel;
        if (fw.position && *fw.position < (int) fw.names.size())
            return ", Filter = " + fw.names[*fw.position];
        return ", Filter = unknown";
    }
}

Poller::Poller(ReaderFactory factory, Timing timing, bool startThread)
    : m_factory(std::move(factory)), m_timing(timing), m_lastConditions(std::chrono::steady_clock::now())
{
    if (startThread)
        m_thread = std::thread(&Poller::Run, this);
}

Poller::~Poller()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        m_stopping = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void Poller::SetContext(const Context& ctx)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (ctx == m_context)
            return;
        m_context = ctx;
        m_contextChanged = true;
    }
    m_wake.notify_all();
}

Snapshot Poller::Latest() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latest;
}

std::vector<std::string> Poller::TakeInfoLines()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> lines(m_lines.begin(), m_lines.end());
    m_lines.clear();
    return lines;
}

void Poller::Publish(const Snapshot& snap, std::vector<std::string> lines)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_latest = snap;
    for (std::string& line : lines)
        m_lines.push_back(std::move(line));
    while (m_lines.size() > MAX_QUEUED_LINES)
        m_lines.pop_front();
}

std::chrono::milliseconds Poller::PollOnce()
{
    Context ctx;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ctx = m_context;
    }
    auto now = std::chrono::steady_clock::now();

    if (!ctx.alpaca)
    {
        m_reader.reset();
        m_work = Snapshot();
        m_work.source = Source::NotAlpaca;
        m_exposing = false;
        m_needFull = true;
        Publish(m_work, { });
        return m_timing.fast;
    }

    if (!m_reader || ctx != m_readerContext)
    {
        m_reader = m_factory(ctx.host, ctx.port);
        m_readerContext = ctx;
        m_needFull = true;
    }
    StoppableReader r(*m_reader, m_stopping);
    Snapshot prev = m_work;

    if (m_needFull || now - m_lastFull >= m_timing.full)
    {
        std::vector<ConfiguredDevice> devices;
        if (!r.ConfiguredDevices(&devices))
        {
            m_work = Snapshot();
            m_work.source = Source::Unavailable;
            m_exposing = false;
            m_needFull = true;
            Publish(m_work, { });
            return m_timing.retry;
        }

        Snapshot s;
        s.source = Source::Alpaca;
        std::string name;
        if (!r.ServerName(&name) || name.empty())
            name = "Alpaca";
        s.server = name + " " + ctx.host + ":" + std::to_string(ctx.port);

        m_selection = SelectDevices(devices, ctx.guideCamera, ctx.telescope, ctx.rotator);
        m_camera = ChooseCamera(r, m_selection.cameraCandidates, &s.ambiguousCameras);
        if (m_selection.telescope)
            s.telescope = ReadTelescope(r, *m_selection.telescope);
        if (m_camera)
            s.camera = ReadCamera(r, *m_camera);
        if (m_selection.filterWheel)
            s.filterWheel = ReadFilterWheel(r, *m_selection.filterWheel);
        if (m_selection.rotator)
            s.rotator = ReadRotator(r, *m_selection.rotator);
        if (m_selection.focuser)
            s.focuser = ReadFocuser(r, *m_selection.focuser);
        if (m_selection.conditions)
            s.conditions = ReadConditions(r, *m_selection.conditions);

        m_work = s;
        m_lastFull = now;
        m_needFull = false;
    }
    else
    {
        if (m_work.filterWheel)
            ReadFilterPosition(r, *m_selection.filterWheel, &*m_work.filterWheel);
        if (m_work.rotator)
            ReadRotatorPosition(r, *m_selection.rotator, &*m_work.rotator);
        if (m_work.focuser)
            ReadFocuserPosition(r, *m_selection.focuser, &*m_work.focuser);
    }

    std::vector<std::string> lines = EquipmentChangeLines(prev, m_work);

    if (m_camera)
    {
        std::optional<int> state = ReadInt(r, Path("camera", *m_camera, "camerastate"));
        if (state)
        {
            bool exposing = Exposing(state);
            if (exposing && !m_exposing)
                lines.push_back("INFO: IMAGING EXPOSURE start, Duration = unknown" + FilterSuffix(m_work));
            else if (!exposing && m_exposing)
            {
                std::optional<double> d = ReadDouble(r, Path("camera", *m_camera, "lastexposureduration"));
                char buf[64];
                snprintf(buf, sizeof(buf), "%.1f s", d.value_or(0.0));
                lines.push_back(std::string("INFO: IMAGING EXPOSURE end, Duration = ") + (d ? buf : "unknown"));
            }
            m_exposing = exposing;
        }
    }
    else
        m_exposing = false;

    if (m_work.conditions && now - m_lastConditions >= m_timing.conditions)
    {
        lines.push_back(ConditionsLine(m_work));
        m_lastConditions = now;
    }

    Publish(m_work, std::move(lines));
    return m_timing.fast;
}

void Poller::Run()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_stop)
    {
        lock.unlock();
        std::chrono::milliseconds wait = PollOnce();
        lock.lock();
        m_wake.wait_for(lock, wait, [this] { return m_stop || m_contextChanged; });
        m_contextChanged = false;
    }
}
}
