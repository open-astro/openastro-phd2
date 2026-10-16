/*
 *  imaging_gear_alpaca.h
 *  PHD Guiding
 *
 *  Connects the imaging-gear poller (imaging_gear_poller.h) to the Alpaca
 *  server the guider uses, for the guide log. Main thread only.
 */

#ifndef IMAGING_GEAR_ALPACA_H_INCLUDED
#define IMAGING_GEAR_ALPACA_H_INCLUDED

#include <string>
#include <vector>

namespace ImagingGearLog
{
// start/stop the background reader
void Start();
void Stop();

// tell the reader what the guider is connected to; call after equipment
// connects or disconnects
void EquipmentChanged();

// the header lines for a guiding or calibration section (discards INFO lines
// queued before it, since the header describes the current state)
std::string Header();

// INFO lines queued since the last call
std::vector<std::string> TakeInfoLines();
}

#endif
