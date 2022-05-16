#include <pins.h> // rename pins.h.example and adjust pins
#include <LogicData.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <Credentials.h> // rename Credential.h.example and adjust variables
#include <Logging.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

int btn_pins[] = {BTN_UP, BTN_DOWN};
const int btn_pressed_state = HIGH;// when we read high, button is pressed
const uint32_t debounce_time = 50;
const uint32_t double_time = 500;

#define BTN_COUNT ARRAY_SIZE(btn_pins)
int8_t btn_last_state[BTN_COUNT] = {-1};
int8_t btn_last_double[BTN_COUNT] = {-1};
uint32_t debounce[BTN_COUNT] = {0};
uint32_t btn_last_on[BTN_COUNT] = {0};


//last_signal is just the last time input was read from buttons or from controller
//If we haven't seen anything from either in a bit, stop moving
uint32_t last_signal = 0;
uint32_t signal_giveup_time = 2000;

long lastPublish = 0;
uint8_t publishedHeight = 0;

const char* versionLine = "Robodesk v2.5  build: " __DATE__ " " __TIME__;
LogicData ld(-1);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

//-- Buffered mode parses input words and sends them to output separately
void IRAM_ATTR logicDataPin_ISR() {
  ld.PinChange(HIGH == digitalRead(LOGICDATA_RX));
}

uint8_t highTarget = 125;
uint8_t lowTarget = 88;
uint8_t maxHeight = 128; //maxHeight table = 128, but you may set a custom min
uint8_t minHeight = 78; //minHeight table = 62, but you may set a custom min
bool setHeight = false;
enum Directions { UP, DOWN, STOPPED };
Directions direction = STOPPED;
Directions last_direction = STOPPED;
bool mqttLog = false;

uint8_t currentHeight;
uint8_t targetHeight;

#pragma region Helpers

template <class T>
char * convertToChar (T value) {
    String valueString = String(value);
    char * output = new char[valueString.length() +1];
    strcpy(output,valueString.c_str());
    return output;
}
int convertToInt (char* value, int length) {
  value[length] = '\0';
  return atoi(value);
}

/**
 * @brief checks if passed height is within allowed range
 *        also allows for up/down movement if below/above min/max
 * 
 * @param checkHeight 
 * @param direction 
 * @return true 
 * @return false 
 */
bool isValidHeight(int checkHeight, Directions direction = STOPPED) {
  if (checkHeight >= minHeight && checkHeight <= maxHeight)
    return true;
  else {
    // allow to move in the direction away from min/max
    if (direction == UP && checkHeight <= maxHeight)
      return true;
    else if (direction == DOWN && checkHeight >= minHeight)
      return true;
    else
      return false;
  }
}

void log(const char* message) {
  Serial.println(message);

  // if (mqttLog == true) {
  //   mqttClient.publish((MQTT_TOPIC + "log").c_str(), message);
  // }
}

#pragma endregion

/**
 * @brief Checks the display - the display being the (non-existing)
 *        remote display to the motor fo the table
 *        Records the last time the display changed & currentHeigt
 */
void check_display() {
  static uint32_t prev = 0;
  uint32_t msg = ld.ReadTrace();
  char buf[80];
  if (msg) {
    uint32_t now = millis();
    sprintf(buf, "%6ums %s: %s", now - prev, ld.MsgType(msg), ld.Decode(msg));
    Serial.println(buf);
    log(msg);
    prev=now;
  }

  // Reset idle-activity timer if display number changes or if any other display activity occurs (i.e. display-ON)
  if (ld.IsNumber(msg)) {
    auto new_height = ld.GetNumber(msg);
    if (new_height == currentHeight) {
      return;
    }
    currentHeight = new_height;
  }
  if (msg)
    last_signal = millis();
}

enum Actions { UpSingle, UpDouble, DownSingle, DownDouble };
bool latch_up = false;
bool latch_down = false;

void transitionState(enum Actions action) {
  switch(action) {
    case UpSingle:
    case DownSingle:
      if (latch_up || latch_down) {
        log("Breaking latches");
        latch_up = false;
        latch_down = false;
      }
      break;
    case UpDouble:
      latch_up = true;
      targetHeight = highTarget;
      Serial.print("Latching UP ");
      Serial.println(targetHeight);
      break;
    case DownDouble:
      latch_down = true;
      targetHeight = lowTarget;
      Serial.print("Latching Down ");
      Serial.println(targetHeight);
      break;
  }
}

#pragma region Table Movement

void stop_table() {
    digitalWrite(ASSERT_UP, LOW);
    digitalWrite(ASSERT_DOWN, LOW);
    latch_up = false;
    latch_down = false;
    targetHeight = currentHeight;
    setHeight = false;
    last_direction = STOPPED;

    if (direction != STOPPED) {
      mqttClient.publish((MQTT_TOPIC + "state").c_str(), "stopped");
      direction = STOPPED;
    }
}

void move_table(Directions tmpDirection) {
  // currentHeight is initially 0 before the first move
  if (currentHeight == 0 || isValidHeight(currentHeight, tmpDirection)) {
    // move the table up or down setting the pins to high/low
    digitalWrite(ASSERT_UP, (tmpDirection == UP ? HIGH : LOW));
    digitalWrite(ASSERT_DOWN, (tmpDirection == DOWN ? HIGH : LOW));

    //make sure to only log if there was a change
    if (direction != tmpDirection) {
      mqttClient.publish((MQTT_TOPIC + "state").c_str(), (tmpDirection == UP ? "up" : "down"));
      direction = tmpDirection;
      last_direction = tmpDirection;
    }
  } else if (!isValidHeight(currentHeight, tmpDirection)) {
    stop_table();
  }
}

void move() {
  //btn_last_state has the current buttons pressed
  if(btn_last_state[0] && btn_last_state[1]) {
    //both buttons pressed, do nothing
    //TODO: Save position to EEPROM like https://github.com/talsalmona/RoboDesk/blob/master/RoboDesk.ino
    log("Both buttons pressed");
  } else if(btn_last_state[0]) {
    //left button pressed
    move_table(UP);
    return;
  } else if(btn_last_state[1]) {
    //right button pressed
    move_table(DOWN);
    return;
  } else if (!latch_up && !latch_down && !setHeight) {
    //if not latched, do nothing
    stop_table();
    return;
  }

  if (latch_up && latch_down) {
    log("Latch up and latch down set, this is an issue");
    while(true) ;
  }

  if((millis() - last_signal > signal_giveup_time) && !setHeight) {
    log("Haven't seen input in a while, turning everything off for safety");
    stop_table();
    while(true) ;
  }

  if(currentHeight != targetHeight) {
    if (latch_up || latch_down) {
      //should be moving
      latch_up ? move_table(UP) : move_table(DOWN);
      digitalWrite(!latch_up ? ASSERT_UP : ASSERT_DOWN, LOW);
      return;
    } else if (setHeight) {
      if (currentHeight > targetHeight)
        move_table(DOWN);
      else
        move_table(UP);
      return;
    }
  } else {
    Serial.print("Hit targetHeight ");
    Serial.println(targetHeight);
    stop_table();
    return;
  }
}

#pragma endregion

#pragma region MQTT Callback

void mqtt_callCmd(String message) {
    Log.Debug("MQTT: Inside cmd topic" CR);
    if (message == "debug") {
      if (mqttLog == false)
        mqttLog = true;
      else
        mqttLog = false;
      log(mqttLog ? "Activated MQTT Logging" : "Deactivated MQTT Logging");
    } else if (message == "up") {
        setHeight = true;
        targetHeight = highTarget;
    } else if (message == "down") {
        setHeight = true;
        targetHeight = lowTarget;
    } else if (message == "test") {
        Log.Debug("MQTT: Current height: %d" CR, currentHeight);
    } else if (message == "stop") {
        stop_table();
    }
}

void mqtt_callSet(byte* message, int length) {
    Log.Debug("MQTT: Inside set topic" CR);
    int height_in = convertToInt((char* )message, length);
    if (isValidHeight(height_in)) {
      targetHeight = height_in;
      setHeight = true;
    } else {
      char buffer[40];
      sprintf(buffer, "Invalid height! [min: %d, max: %d]", minHeight, maxHeight);
      log(buffer);
    }
    
    Serial.println("------");
    Serial.print("Current height: ");
    Serial.println(currentHeight);
}

void mqttCallback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: " CR);
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;
  
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  log("");

  if ((String) topic == MQTT_TOPIC + "set") {
    mqtt_callSet(message, length);
  }

  if ((String) topic == MQTT_TOPIC + "cmd") {
    mqtt_callCmd(messageTemp);
  }
}

#pragma endregion

#pragma region Setup: Wifi, OTA, MQTT

void setup_wifi() {
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    #ifdef WIFICONFIG_H
      WiFi.config(WIFI_IP, WIFI_DNS, WIFI_GATEWAY, WIFI_SUBNET);
    #endif
    WiFi.setAutoReconnect(true);
    WiFi.hostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PSK);
 
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
    }

    Serial.println("---------");
    Serial.print("Wifi connected with IP: ");
    Serial.println(WiFi.localIP());
}

void setup_OTA() {
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    log("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      log("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      log("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      log("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      log("Receive Failed");
    else if (error == OTA_END_ERROR)
      log("End Failed");
  });
  ArduinoOTA.begin();
}

void initMQTT() {
  if (!mqttClient.connected()) {
      while (!mqttClient.connected()) {
          if (mqttClient.connect(HOSTNAME,
              MQTT_USER, MQTT_PASS,
              (MQTT_TOPIC + "lastConnected").c_str(),
              0,
              true,
              versionLine)) {
            mqttClient.subscribe((MQTT_TOPIC + "set").c_str());
            mqttClient.subscribe((MQTT_TOPIC + "cmd").c_str());
          }
          delay(100);
      }
  } else
    mqttClient.loop();
}

#pragma endregion

#pragma region MQTT functions
void mqtt_publishHeight() {
    long now = millis();
    if (publishedHeight != currentHeight && now - lastPublish > 2000)
    {
        lastPublish = now;
        publishedHeight = currentHeight;
        mqttClient.publish((MQTT_TOPIC + "height").c_str(), convertToChar(currentHeight));
    }
}
#pragma endregion

void setup() {

  pinMode(BTN_UP, INPUT);
  pinMode(BTN_DOWN, INPUT);
  pinMode(LOGICDATA_RX, INPUT);

  pinMode(ASSERT_UP, OUTPUT);
  pinMode(ASSERT_DOWN, OUTPUT);

  Serial.begin(115200);
  Log.Init(LOG_LEVEL_DEBUG, 115200L);

  setup_wifi();
  setup_OTA();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  logicDataPin_ISR();
  attachInterrupt(digitalPinToInterrupt(LOGICDATA_RX), logicDataPin_ISR, CHANGE);

  ld.Begin();

  log(versionLine);

  // we use this just to get an initial height on startup (otherwise it's 0)
  move_table(UP);
}

void loop() {
  // sets global currentHeight and last_signal from logicdata serial
  check_display();

  ArduinoOTA.handle();
  initMQTT();

  if (!setHeight) {
    for(uint8_t i=0; i < ARRAY_SIZE(btn_pins); ++i) {
      int btn_state = digitalRead(btn_pins[i]);
      if((btn_state == btn_pressed_state) != btn_last_state[i] && millis() - debounce[i] > debounce_time) {
        //change state
        btn_last_state[i] = (btn_state == btn_pressed_state);
        debounce[i] = millis();
        last_signal = debounce[i];

        if(btn_last_state[i]) {
          if(millis() - btn_last_on[i] < double_time) {
            //double press
            Serial.print("double press ");
            Serial.println(i == 0 ? "up" : "down");
            mqttClient.publish((MQTT_TOPIC + "button").c_str(), i == 0 ? "double up" : "double down");
            transitionState(i == 0 ? UpDouble : DownDouble);
          } else {
            btn_last_on[i] = debounce[i];
            //single press
            Serial.print("single press ");
            Serial.println(i == 0 ? "up" : "down");
            mqttClient.publish((MQTT_TOPIC + "button").c_str(), i == 0 ? "single up" : "single down");
            transitionState(i == 0 ? UpSingle : DownSingle);
          }
        }// endif pressed
      }
    }
  }

  move();
  mqtt_publishHeight();
}
