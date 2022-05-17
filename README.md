# RoboDesk
Read LOGICDATA protocol to control height of a powered sit/stand desk.

LOGICDATA protocol decoding from [phord's RoboDesk](https://github.com/phord/RoboDesk/tree/LogicData)

# Setup
Make sure to rename all *.example files and adjust variables, as of now we have:
* firmware/platformio.ini.example
* firmware/src/Credentials.h.example
* firmware/src/pins.h.example
* firmware/src/Wificonfig.h.example

## Features:
* Double tap a direction to go to a hardcoded target height
* Leverage MQTT to get / set height

## Files:
* `firmware`: platformio code for the d1 mini
* `schematic\v2`: kicad schematic for the connections between the d1 mini and the desk
  * One thing I did is diagram both sides of a bunch of connectors.
  Is this the best way to do this?
* `schematic\v2\case\robodesk-case.scad`: Enclosure using https://www.thingiverse.com/thing:1264391
* `images\logicdata-controller.xcf`: orthorectified top and bottom image of the logicdata controller `SMART-e-2-UNL-US`

## Hardware
* [Wemos/LOLIN D1 mini](https://www.wemos.cc/en/latest/d1/d1_mini.html)
* [Wemos ProtoBoard Shield](https://www.wemos.cc/en/latest/d1_mini_shield/protoboard.html)
* 2x 10kΩ pull-down resistors
* Circular DIN 7P male & female connectors

## Links
* Blog post for [version 1](https://github.com/mtfurlan/RoboDesk/releases/tag/v1.0.0): https://technicallycompetent.com/hacking-logicdata-desk/
* Blog post for [version 2](https://github.com/mtfurlan/RoboDesk/releases/tag/v2.0.0): https://technicallycompetent.com/logicdata-desk-v2/