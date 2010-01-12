/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009

	M Roberts (original release)
	Robin Birch <robinb@ruffnready.co.uk>
	Samuel Gisiger <samuel.gisiger@triadis.ch>
	Jeff Goodenough <jeff@enborne.f2s.com>
	Alastair Harrison <aharrison@magic.force9.co.uk>
	Scott Penrose <scottp@dd.com.au>
	John Wharington <jwharington@gmail.com>
	Lars H <lars_hn@hotmail.com>
	Rob Dunning <rob@raspberryridgesheepfarm.com>
	Russell King <rmk@arm.linux.org.uk>
	Paolo Ventafridda <coolwind@email.it>
	Tobias Lohner <tobias@lohner-net.de>
	Mirek Jezek <mjezek@ipplc.cz>
	Max Kellermann <max@duempel.org>
	Tobias Bieniek <tobias.bieniek@gmx.de>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "Device/Descriptor.hpp"
#include "Device/Driver.hpp"
#include "Device/Parser.hpp"
#include "Device/FLARM.hpp"
#include "DeviceBlackboard.hpp"
#include "Protection.hpp"
#include "NMEA/Info.hpp"
#include "Thread/Mutex.hpp"
#include "StringUtil.hpp"

#include <assert.h>

bool
DeviceDescriptor::Open(int _port)
{
  Port = _port;

  if (Driver == NULL)
    return false;

  assert(Driver->CreateOnComPort != NULL);

  parser.Reset();

  device = Driver->CreateOnComPort(Com);
  if (!device->Open()) {
    delete device;
    device = NULL;
    return false;
  }

  return true;
}

void
DeviceDescriptor::Close()
{
  if (device != NULL) {
    delete device;
    device = NULL;
  }

  ComPort *OldCom = Com;
  Com = NULL;

  if (OldCom != NULL)
    delete OldCom;
}

bool
DeviceDescriptor::OpenLog(const TCHAR *FileName)
{
  fhLogFile = _tfopen(FileName, _T("a+b"));
  return fhLogFile != NULL;
}

void
DeviceDescriptor::CloseLog()
{
  if (fhLogFile == NULL)
    return;

  fclose(fhLogFile);
  fhLogFile = NULL;
}

bool
DeviceDescriptor::IsLogger() const
{
  return Driver != NULL &&
    ((Driver->Flags & drfLogger) != 0 ||
     (device != NULL && device->IsLogger()) ||
     parser.isFlarm);
}

bool
DeviceDescriptor::IsGPSSource() const
{
  return Driver != NULL &&
    ((Driver->Flags & drfGPS) != 0 ||
     (device != NULL && device->IsGPSSource()));
}

bool
DeviceDescriptor::IsBaroSource() const
{
  return Driver != NULL &&
    ((Driver->Flags & drfBaroAlt) != 0 ||
     (device != NULL && device->IsBaroSource()));
}

bool
DeviceDescriptor::IsRadio() const
{
  return Driver != NULL && (Driver->Flags & drfRadio) != 0;
}

bool
DeviceDescriptor::IsCondor() const
{
  return Driver != NULL && (Driver->Flags & drfCondor) != 0;
}

bool
DeviceDescriptor::ParseNMEA(const TCHAR *String, NMEA_INFO *GPS_INFO)
{
  assert(String != NULL);
  assert(GPS_INFO != NULL);

  if (fhLogFile != NULL && String != NULL && !string_is_empty(String)) {
    char sTmp[500]; // temp multibyte buffer
    const TCHAR *pWC = String;
    char *pC = sTmp;
    //    static DWORD lastFlush = 0;

    sprintf(pC, "%9u <", (unsigned)GetTickCount());
    pC = sTmp + strlen(sTmp);

    while (*pWC) {
      if (*pWC != '\r') {
        *pC = (char)*pWC;
        pC++;
      }
      pWC++;
    }
    *pC++ = '>';
    *pC++ = '\r';
    *pC++ = '\n';
    *pC++ = '\0';

    fputs(sTmp, fhLogFile);
  }

  if (pDevPipeTo && pDevPipeTo->Com) {
    // stream pipe, pass nmea to other device (NmeaOut)
    // TODO code: check TX buffer usage and skip it if buffer is full (outbaudrate < inbaudrate)
    pDevPipeTo->Com->WriteString(String);
  }

  if (device != NULL && device->ParseNMEA(String, GPS_INFO, enable_baro)) {
    GPS_INFO->Connected = 2;
    return true;
  }

  if (String[0] == '$') { // Additional "if" to find GPS strings
    if (parser.ParseNMEAString_Internal(String, GPS_INFO)) {
      GPS_INFO->Connected = 2;
      return true;
    }
  }

  return false;
}

bool
DeviceDescriptor::PutMacCready(double MacCready)
{
  return device != NULL ? device->PutMacCready(MacCready) : true;
}

bool
DeviceDescriptor::PutBugs(double bugs)
{
  return device != NULL ? device->PutBugs(bugs) : true;
}

bool
DeviceDescriptor::PutBallast(double ballast)
{
  return device != NULL ? device->PutBallast(ballast) : true;
}

bool
DeviceDescriptor::PutVolume(int volume)
{
  return device != NULL ? device->PutVolume(volume) : true;
}

bool
DeviceDescriptor::PutActiveFrequency(double frequency)
{
  return device != NULL ? device->PutActiveFrequency(frequency) : true;
}

bool
DeviceDescriptor::PutStandbyFrequency(double frequency)
{
  return device != NULL ? device->PutStandbyFrequency(frequency) : true;
}

bool
DeviceDescriptor::PutQNH(const AtmosphericPressure& pres)
{
  return device != NULL ? device->PutQNH(pres) : true;
}

bool
DeviceDescriptor::PutVoice(const TCHAR *sentence)
{
  return device != NULL ? device->PutVoice(sentence) : true;
}

void
DeviceDescriptor::LinkTimeout()
{
  if (device != NULL)
    device->LinkTimeout();
}

bool
DeviceDescriptor::Declare(const struct Declaration *declaration)
{
  bool result = (device != NULL) && (device->Declare(declaration));

  if (parser.isFlarm)
    result = FlarmDeclare(Com, declaration) || result;

  return result;
}

void
DeviceDescriptor::OnSysTicker()
{
  if (device == NULL)
    return;

  ticker = !ticker;
  if (ticker)
    // write settings to vario every second
    device->OnSysTicker();
}

void
DeviceDescriptor::LineReceived(const TCHAR *line)
{
  ScopeLock protect(mutexBlackboard);
  ParseNMEA(line, &device_blackboard.SetBasic());
}
