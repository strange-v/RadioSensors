#include <functions.h>

void transmitData(NodeData data)
{
  bool state = radio.sendWithRetry(RADIO_GATEWAY_ID, &data, sizeof(data), 3);
#ifdef RADIO_NODE_DEBUG_LED
  blink(state ? 1 : 3);
#endif
  
  radio.sleep();
}