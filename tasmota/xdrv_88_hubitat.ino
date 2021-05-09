/*
  xdrv_88_hubitat.ino - Hubitat integration for Tasmota devices

  Copyright (C) 2020  John Clark (inindev), Eric Maycock (erocm123)

  Allows for the integration of Tasmota devices directly to the
  Hubitat Elevation® hub without the need for an MQTT broker.

  This integration was originally conceived and produced by
  Eric Maycock (erocm123): github.com/erocm123/Sonoff-Tasmota

  The following is a derivative work that modernizes and simplifies
  Eric's integration strategy to better fit my own needs.  UDP
  auto-discovery has been removed, and the previous Hubitat drivers
  and application for Sonoff have been reduced to a single driver:
  github.com/inindev/hubitat/blob/main/drivers/tasmota_device.groovy

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define XDRV_88               88

#include <ESP8266HTTPClient.h>

WiFiClient espClient;      // wifi client
HTTPClient espHttpClient;  // http client

const char kHubitatCommands[] PROGMEM = "|"  // no prefix
  D_CMND_HUBITATHOST "|" D_CMND_HUBITATPORT;

void (* const HubitatCommand[])(void) PROGMEM = {
   &CmndHubitatHost, &CmndHubitatPort
};

/*********************************************************************************************/
void HubitatInit(void)
{
  espHttpClient.begin(espClient, Settings.hubitat_host, Settings.hubitat_port);
  espHttpClient.setUserAgent(String("Tasmota/") + TasmotaGlobal.version);
}

/*********************************************************************************************/
bool HubitatPublish(const char* data)
{
  if (!Settings.flag5.hubitat_enabled) {
    return false;
  }

  if (!data || data[0] != '{') { // dispatch json messages only
    return false;
  }

  espHttpClient.addHeader("Content-Type", "application/json;charset=utf-8");
  int32_t httpStatus = espHttpClient.POST(reinterpret_cast<const uint8_t*>(data), strlen(data));
  if(httpStatus < 0) {
    yield();
    httpStatus = espHttpClient.POST(reinterpret_cast<const uint8_t*>(data), strlen(data));
  }
  if(httpStatus != 200) {
    if(httpStatus < 0) {
      AddLog_P(LOG_LEVEL_ERROR, PSTR(D_LOG_HUBITAT "http transport error - %s  host: %s:%d"), espHttpClient.errorToString(httpStatus).c_str(), Settings.hubitat_host, Settings.hubitat_port);
    } else {
      AddLog_P(LOG_LEVEL_ERROR, PSTR(D_LOG_HUBITAT "http status error: %d  host: %s:%d"), httpStatus, Settings.hubitat_host, Settings.hubitat_port);
    }
    return false;
  }

  return true;
}

/*********************************************************************************************\
 * Presentation
\*********************************************************************************************/

#ifdef USE_WEBSERVER

#define WEB_HANDLE_HUBITAT "he"

const char HTTP_BTN_MENU_HUBITAT[] PROGMEM =
  "<p><form action='" WEB_HANDLE_HUBITAT "' method='get'><button>" D_CONFIGURE_HUBITAT "</button></form></p>";

const char HTTP_FORM_HUBITAT[] PROGMEM =
  "<fieldset><legend><b>&nbsp;" D_HUBITAT_PARAMETERS "&nbsp;</b></legend>"
  "<form method='get' action='" WEB_HANDLE_HUBITAT "'>"
  "<p><b>" D_HOST "</b> (" HUBITAT_HOST ")<br><input id='hh' placeholder='" HUBITAT_HOST "' value='%s'></p>"
  "<p><b>" D_PORT "</b> (" STR(HUBITAT_PORT) ")<br><input id='hp' placeholder='" STR(HUBITAT_PORT) "' value='%s'></p>";

void HandleHubitatConfiguration(void)
{
  if (!HttpCheckPriviledgedAccess()) { return; }

  AddLog_P(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_CONFIGURE_HUBITAT));

  if (Webserver->hasArg(F("save"))) {
    HubitatSaveSettings();
    WebRestart(1);
    return;
  }

  char hubitat_port_string[8] = "";
  if(Settings.hubitat_port > 0) {
    itoa(Settings.hubitat_port, hubitat_port_string, 10);
  }

  WSContentStart_P(PSTR(D_CONFIGURE_HUBITAT));
  WSContentSendStyle();
  WSContentSend_P(HTTP_FORM_HUBITAT,
    Settings.hubitat_host,
    hubitat_port_string);
  WSContentSend_P(HTTP_FORM_END);
  WSContentSpaceButton(BUTTON_CONFIGURATION);
  WSContentStop();
}

void HubitatSaveSettings(void)
{
  char tmp[TOPSZ];
  WebGetArg(PSTR("hh"), tmp, sizeof(tmp));
  strlcpy(Settings.hubitat_host, (!strlen(tmp)) ? PSTR(HUBITAT_HOST) : (!strcmp(tmp,"0")) ? "" : tmp, sizeof(Settings.hubitat_host));
  WebGetArg(PSTR("hp"), tmp, sizeof(tmp));
  Settings.hubitat_port = (!strlen(tmp)) ? HUBITAT_PORT : atoi(tmp);
  AddLog_P(LOG_LEVEL_INFO, PSTR(D_LOG_HUBITAT D_CMND_HUBITATHOST " %s, " D_CMND_HUBITATPORT " %d"), Settings.hubitat_host, Settings.hubitat_port);
}
#endif  // USE_WEBSERVER

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

void CmndHubitatHost(void)
{
  if ((XdrvMailbox.data_len > 0) && (XdrvMailbox.data_len < sizeof(Settings.hubitat_host))) {
    strlcpy(Settings.hubitat_host, (SC_CLEAR == Shortcut()) ? "" : (SC_DEFAULT == Shortcut()) ? HUBITAT_HOST : XdrvMailbox.data, sizeof(Settings.hubitat_host));
    TasmotaGlobal.restart_flag = 2;
  }
  ResponseCmndChar(Settings.hubitat_host);
}

void CmndHubitatPort(void)
{
  if ((XdrvMailbox.payload > 0) && (XdrvMailbox.payload < 65536)) {
    Settings.hubitat_port = (1 == XdrvMailbox.payload) ? HUBITAT_PORT : XdrvMailbox.payload;
    TasmotaGlobal.restart_flag = 2;
  }
  ResponseCmndNumber(Settings.hubitat_port);
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv88(uint8_t function)
{
  if (!Settings.flag5.hubitat_enabled) return false;

  bool result = false;
  switch (function) {
    case FUNC_PRE_INIT:
      HubitatInit();
      break;
    case FUNC_LOOP:
      break;
#ifdef USE_WEBSERVER
    case FUNC_WEB_ADD_BUTTON:
      WSContentSend_P(HTTP_BTN_MENU_HUBITAT);
      break;
    case FUNC_WEB_ADD_HANDLER:
      WebServer_on(PSTR("/" WEB_HANDLE_HUBITAT), HandleHubitatConfiguration);
      break;
#endif  // USE_WEBSERVER
    case FUNC_COMMAND:
      result = DecodeCommand(kHubitatCommands, HubitatCommand);
      break;
  }
  return result;
}
