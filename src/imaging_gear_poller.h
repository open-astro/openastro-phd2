/*
 *  imaging_gear_poller.h
 *  PHD Guiding
 *
 *  Reads the imaging train from the Alpaca server on a background thread and
 *  keeps the latest Snapshot in a cache, so the guide loop never waits on the
 *  network. Read-only: Reader has no way to write to a device. Kept free of wx
 *  so it can be unit tested (tests/imaging_gear/).
 */

#ifndef IMAGING_GEAR_POLLER_H_INCLUDED
#define IMAGING_GEAR_POLLER_H_INCLUDED

#include "imaging_gear.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace ImagingGear
{
// GET-only access to one Alpaca server; paths are like "camera/1/name".
// Every call returns false for anything the driver doesn't report.
class Reader
{
public:
    virtual ~Reader() = default;
    virtual bool ConfiguredDevices(std::vector<ConfiguredDevice> *devices) = 0;
    virtual bool ServerName(std::string *name) = 0;
    virtual bool GetString(const std::string& path, std::string *value) = 0;
    virtual bool GetDouble(const std::string& path, double *value) = 0;
    virtual bool GetInt(const std::string& path, int *value) = 0;
    virtual bool GetBool(const std::string& path, bool *value) = 0;
    virtual bool GetStringList(const std::string& path, std::vector<std::string> *value) = 0;
    virtual bool GetIntList(const std::string& path, std::vector<int> *value) = 0;
};

// What the guider is connected to; set from the main thread.
struct Context
{
    bool alpaca = false; // false: the guider is not connected through Alpaca
    std::string host;
    long port = 0;
    int guideCamera = -1; // Alpaca device numbers, -1 when not connected through Alpaca
    int telescope = -1;
    int rotator = -1;

    bool operator==(const Context& o) const
    {
        return alpaca == o.alpaca && host == o.host && port == o.port && guideCamera == o.guideCamera &&
            telescope == o.telescope && rotator == o.rotator;
    }
    bool operator!=(const Context& o) const { return !(*this == o); }
};

class Poller
{
public:
    using ReaderFactory = std::function<std::unique_ptr<Reader>(const std::string& host, long port)>;

    struct Timing
    {
        std::chrono::milliseconds fast { 1000 }; // camera state, filter, rotator, focuser
        std::chrono::milliseconds full { 30000 }; // everything, and device discovery
        std::chrono::milliseconds conditions { 60000 }; // INFO: CONDITIONS
        std::chrono::milliseconds retry { 10000 }; // after the server could not be read
    };

    // startThread false: nothing runs until PollOnce() is called (tests)
    Poller(ReaderFactory factory, Timing timing, bool startThread = true);
    ~Poller();

    void SetContext(const Context& ctx);
    Snapshot Latest() const;
    // INFO: EQUIPMENT / IMAGING EXPOSURE / CONDITIONS lines since the last call
    std::vector<std::string> TakeInfoLines();

    // one poll cycle; returns how long to wait before the next one
    std::chrono::milliseconds PollOnce();

private:
    void Run();
    void Publish(const Snapshot& snap, std::vector<std::string> lines);

    ReaderFactory m_factory;
    Timing m_timing;

    mutable std::mutex m_mutex; // guards the members below; never held during I/O
    std::condition_variable m_wake;
    bool m_stop = false;
    std::atomic<bool> m_stopping { false }; // read without m_mutex by the poll thread
    bool m_contextChanged = false;
    Context m_context;
    Snapshot m_latest;
    std::deque<std::string> m_lines;

    // poll thread state
    std::unique_ptr<Reader> m_reader;
    Context m_readerContext;
    Selection m_selection;
    std::optional<int> m_camera;
    bool m_exposing = false;
    Snapshot m_work;
    Snapshot m_reported; // last settled positions, for INFO: EQUIPMENT lines
    std::chrono::steady_clock::time_point m_lastFull, m_lastConditions;
    bool m_needFull = true;
    int m_failedDiscoveries = 0;

    std::thread m_thread;
};
}

#endif
