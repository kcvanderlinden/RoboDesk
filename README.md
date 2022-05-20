# RoboDesk
Read LOGICDATA protocol to control height of a powered sit/stand desk.

LOGICDATA protocol decoding from [phord's RoboDesk](https://github.com/phord/RoboDesk/tree/LogicData)

# Setup
Make sure to rename all *.example files and adjust variables, as of now we have:
* firmware/platformio.ini.example
* firmware/src/Credentials.h.example
* firmware/src/pins.h.example
* firmware/src/Wificonfig.h.example (optional)

## Features:
* Double tap a direction to go to a hardcoded target height
* Leverage MQTT to get / set height
  * Subscribed topics:
    * `<MQTT_TOPIC>/set` > e.g. `90` (sets the table height to 90 cm)
    * `<MQTT_TOPIC>/cmd` > ...
      * `up` (moves the table to the predefined high position `highTarget`)
      * `down` (moves the table to the predefined low position `lowTarget`)
      * `stop` (stops the table immediately)
      * `ping` (answers with `pong` on the same topic)
  * Published topics:
    * `<MQTT_TOPIC>/state` (up/down/stopped)
    * `<MQTT_TOPIC>/height` (height in cm)
    * `<MQTT_TOPIC>/button` (single/double up/down)
    * `<MQTT_TOPIC>/lastConnected` (will set and retained on connect with current version / build number)

## Files:
* `firmware`: platformio code for the d1 mini
* `schematic`: kicad schematic for the connections between the d1 mini and the desk
  * Two different but similar versions: `desk-schematic` and `Layout-Wemos-ProtoBoard`
* `schematic\case\robodesk-case.scad`: Enclosure using https://www.thingiverse.com/thing:1264391
* `images\logicdata-controller.xcf`: orthorectified top and bottom image of the logicdata controller `SMART-e-2-UNL-US`

## Hardware
* [Wemos/LOLIN D1 mini](https://www.wemos.cc/en/latest/d1/d1_mini.html)
* [Wemos ProtoBoard Shield](https://www.wemos.cc/en/latest/d1_mini_shield/protoboard.html)
* 2x 10kΩ pull-down resistors
* Circular DIN 7P male & female connectors
* 3PDT On-On switch (e.g. MIYAMA MS 500M-B)

## Links
* Blog post for [version 1](https://github.com/mtfurlan/RoboDesk/releases/tag/v1.0.0): https://technicallycompetent.com/hacking-logicdata-desk/
* Blog post for [version 2](https://github.com/mtfurlan/RoboDesk/releases/tag/v2.0.0): https://technicallycompetent.com/logicdata-desk-v2/