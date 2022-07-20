#include <Network.h>

void networkEvent(WiFiEvent_t event)
{
#ifdef ETH_PHY_TYPE
  switch (event)
  {
  case ARDUINO_EVENT_ETH_START:
    debugPrint("ETH Started");
    ETH.setHostname(moduleSettings.hostname);
    break;
  case ARDUINO_EVENT_ETH_CONNECTED:
    debugPrint("ETH Connected");
    break;
  case ARDUINO_EVENT_ETH_GOT_IP:
    ethConnected = true;
    initOta();
#ifdef TELNET_DEBUG
    telnet.begin();
#endif
    debugPrint("IP: ");
    debugPrint(ETH.localIP());
    connectToMqtt();
    break;
  case ARDUINO_EVENT_ETH_DISCONNECTED:
    ethConnected = false;
    debugPrint("ETH Disconnected");
    break;
  case ARDUINO_EVENT_ETH_STOP:
    ethConnected = false;
    debugPrint("ETH Stopped");
    break;
  default:
    break;
  }
#else
  switch (event)
  {
  case ARDUINO_EVENT_WIFI_STA_START:
    debugPrint("WiFi Started");
    WiFi.setHostname(moduleSettings.hostname);
    break;
  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    debugPrint("WiFi Connected");
    break;
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    ethConnected = true;
    initOta();
#ifdef TELNET_DEBUG
    telnet.begin();
#endif
    debugPrint("IP: ");
    debugPrint(WiFi.localIP());
    connectToMqtt();
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    ethConnected = false;
    debugPrint("WiFi Disconnected");
    break;
  case ARDUINO_EVENT_WIFI_STA_STOP:
    ethConnected = false;
    debugPrint("WiFi Stopped");
    break;
  default:
    break;
  }
#endif
}

void connectToNetwork()
{
  if (!ethConnected)
  {
#ifdef ETH_PHY_TYPE
    ETH.begin();
#else
    WiFi.begin(WIFI_SSID, WIFI_PWD);
#endif
  }
}

void conectNetworkTimerHandler()
{
  connectToNetwork();
}

void initOta()
{
  ArduinoOTA.setPassword(OTA_PWD);
  ArduinoOTA.begin();
}