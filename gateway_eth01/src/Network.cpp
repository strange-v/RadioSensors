#include <Network.h>

void networkEvent(WiFiEvent_t event)
{
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
}

void connectToNetwork()
{
  if (!ethConnected)
  {
    ETH.begin();
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