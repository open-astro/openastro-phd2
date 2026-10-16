/*
 *  imaging_gear_alpaca.cpp
 *  PHD Guiding
 *
 *  See imaging_gear_alpaca.h.
 */

#include "phd.h"

#include "imaging_gear_alpaca.h"
#include "imaging_gear_poller.h"
#include "alpaca_client.h"

#ifdef ALPACA_CAMERA
# include "cam_alpaca.h"
#endif
#ifdef GUIDE_ALPACA
# include "scope_alpaca.h"
#endif
#ifdef ROTATOR_ALPACA
# include "rotator_alpaca.h"
#endif

#include <memory>

namespace
{
// Every read gives up after 2 s; the guide loop never waits on these.
const long READ_TIMEOUT_MS = 2000;

class AlpacaGearReader : public ImagingGear::Reader
{
    AlpacaClient m_client;

    // the Value of a GET, or null when the driver reports an error
    const json_value *Value(const std::string& path, JsonParser& parser)
    {
        long errorCode = 0;
        if (!m_client.Get(wxString(path), parser, &errorCode))
            return nullptr;
        const json_value *root = parser.Root();
        if (!root || root->type != JSON_OBJECT)
            return nullptr;
        const json_value *value = nullptr;
        json_for_each(n, root)
        {
            if (!n->name)
                continue;
            if (strcmp(n->name, "ErrorNumber") == 0 && n->type == JSON_INT && n->int_value != 0)
                return nullptr;
            if (strcmp(n->name, "Value") == 0)
                value = n;
        }
        return value;
    }

public:
    AlpacaGearReader(const std::string& host, long port) : m_client(wxString(host), port, 0)
    {
        m_client.SetTimeoutMs(READ_TIMEOUT_MS, READ_TIMEOUT_MS);
        m_client.SetLogRequests(false);
    }

    bool ConfiguredDevices(std::vector<ImagingGear::ConfiguredDevice> *devices) override
    {
        JsonParser parser;
        const json_value *list = Value("management/v1/configureddevices", parser);
        if (!list || list->type != JSON_ARRAY)
            return false;
        devices->clear();
        json_for_each(d, list)
        {
            ImagingGear::ConfiguredDevice dev { "", -1, "" };
            json_for_each(p, d)
            {
                if (!p->name)
                    continue;
                if (strcmp(p->name, "DeviceType") == 0 && p->type == JSON_STRING)
                    dev.type = p->string_value;
                else if (strcmp(p->name, "DeviceName") == 0 && p->type == JSON_STRING)
                    dev.name = p->string_value;
                else if (strcmp(p->name, "DeviceNumber") == 0 && p->type == JSON_INT)
                    dev.number = p->int_value;
            }
            if (!dev.type.empty() && dev.number >= 0)
                devices->push_back(dev);
        }
        return true;
    }

    bool ServerName(std::string *name) override
    {
        JsonParser parser;
        const json_value *desc = Value("management/v1/description", parser);
        if (!desc || desc->type != JSON_OBJECT)
            return false;
        json_for_each(p, desc)
        {
            if (p->name && strcmp(p->name, "ServerName") == 0 && p->type == JSON_STRING)
            {
                *name = p->string_value;
                return true;
            }
        }
        return false;
    }

    bool GetString(const std::string& path, std::string *value) override
    {
        JsonParser parser;
        const json_value *v = Value(path, parser);
        if (!v || v->type != JSON_STRING)
            return false;
        *value = v->string_value;
        return true;
    }

    bool GetDouble(const std::string& path, double *value) override
    {
        JsonParser parser;
        const json_value *v = Value(path, parser);
        if (v && v->type == JSON_FLOAT)
            *value = v->float_value;
        else if (v && v->type == JSON_INT)
            *value = v->int_value;
        else
            return false;
        return true;
    }

    bool GetInt(const std::string& path, int *value) override
    {
        JsonParser parser;
        const json_value *v = Value(path, parser);
        if (v && v->type == JSON_INT)
            *value = v->int_value;
        else if (v && v->type == JSON_FLOAT)
            *value = (int) v->float_value;
        else
            return false;
        return true;
    }

    bool GetBool(const std::string& path, bool *value) override
    {
        JsonParser parser;
        const json_value *v = Value(path, parser);
        if (!v || v->type != JSON_BOOL)
            return false;
        *value = v->int_value != 0;
        return true;
    }

    bool GetStringList(const std::string& path, std::vector<std::string> *value) override
    {
        JsonParser parser;
        const json_value *v = Value(path, parser);
        if (!v || v->type != JSON_ARRAY)
            return false;
        value->clear();
        json_for_each(item, v)
        {
            value->push_back(item->type == JSON_STRING ? item->string_value : "");
        }
        return true;
    }

    bool GetIntList(const std::string& path, std::vector<int> *value) override
    {
        JsonParser parser;
        const json_value *v = Value(path, parser);
        if (!v || v->type != JSON_ARRAY)
            return false;
        value->clear();
        json_for_each(item, v)
        {
            value->push_back(item->type == JSON_INT ? item->int_value : 0);
        }
        return true;
    }
};

std::unique_ptr<ImagingGear::Poller> s_poller;

ImagingGear::Context CurrentContext()
{
    ImagingGear::Context ctx;
    bool camera = false, scope = false, rotator = false;
#ifdef ALPACA_CAMERA
    camera = pCamera && pCamera->Connected && dynamic_cast<CameraAlpaca *>(pCamera);
#endif
#ifdef GUIDE_ALPACA
    Scope *theScope = TheScope();
    scope = theScope && theScope->IsConnected() && dynamic_cast<ScopeAlpaca *>(theScope);
#endif
#ifdef ROTATOR_ALPACA
    rotator = pRotator && pRotator->IsConnected() && dynamic_cast<RotatorAlpaca *>(pRotator);
#endif
    ctx.alpaca = camera || scope;
    if (!ctx.alpaca)
        return ctx;
    ctx.host = pConfig->Profile.GetString("/alpaca/host", _T("localhost")).ToStdString();
    ctx.port = pConfig->Profile.GetLong("/alpaca/port", 0);
    ctx.guideCamera = camera ? (int) pConfig->Profile.GetLong("/alpaca/camera_device", 0) : -1;
    ctx.telescope = scope ? (int) pConfig->Profile.GetLong("/alpaca/telescope_device", 0) : -1;
    ctx.rotator = rotator ? (int) pConfig->Profile.GetLong("/alpaca/rotator_device", 0) : -1;
    return ctx;
}
}

namespace ImagingGearLog
{
void Start()
{
    if (s_poller)
        return;
    ImagingGear::Poller::Timing timing;
    timing.fast = std::chrono::milliseconds(2000);
    s_poller.reset(new ImagingGear::Poller([](const std::string& host, long port)
                                           { return std::unique_ptr<ImagingGear::Reader>(new AlpacaGearReader(host, port)); },
                                           timing));
    EquipmentChanged();
}

void Stop()
{
    s_poller.reset();
}

void EquipmentChanged()
{
    if (s_poller)
        s_poller->SetContext(CurrentContext());
}

std::string Header()
{
    if (!s_poller)
        return "";
    EquipmentChanged();
    s_poller->TakeInfoLines();
    return ImagingGear::HeaderLines(s_poller->Latest(), pFrame->GetFocalLength());
}

std::vector<std::string> TakeInfoLines()
{
    if (!s_poller)
        return { };
    return s_poller->TakeInfoLines();
}
}
