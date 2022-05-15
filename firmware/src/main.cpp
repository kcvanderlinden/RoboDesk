#include <pins.h> // rename pins.h.example and adjust pins
#include <LogicData.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <Credentials.h> // rename Credential.h.example and adjust variables

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


LogicData ld(-1);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

//-- Buffered mode parses input words and sends them to output separately
void IRAM_ATTR logicDataPin_ISR() {
  ld.PinChange(HIGH == digitalRead(LOGICDATA_RX));
}

uint8_t highTarget = 110;
uint8_t lowTarget = 80;
uint8_t maxHeight = 128;
uint8_t minHeight = 62;
bool setheight = false;
bool active = false;
bool directionUp = false;

uint8_t currentHeight;
uint8_t targetHeight;

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

bool isValidHeight(int checkHeight) {
  if (checkHeight >= minHeight && checkHeight <= maxHeight)
    return true;
  else
    return false;
}

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

void mqttCallback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;
  
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  if ((String) topic == MQTT_TOPIC + "set") {
    int height_in = convertToInt((char* )message, length);
    if (isValidHeight(height_in)) {
      targetHeight = height_in;
      setheight = true;
    } else {
      char buffer[40];
      sprintf(buffer, "Invalid height! [min: %d, max: %d]", minHeight, maxHeight);
      Serial.println(buffer);
    }
    
    Serial.println("------");
    Serial.print("Current height: ");
    Serial.println(currentHeight);
  }
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
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void setup() {

  pinMode(BTN_UP, INPUT);
  pinMode(BTN_DOWN, INPUT);
  pinMode(LOGICDATA_RX, INPUT);

  pinMode(ASSERT_UP, OUTPUT);
  pinMode(ASSERT_DOWN, OUTPUT);

  Serial.begin(115200);

  setup_wifi();
  setup_OTA();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  logicDataPin_ISR();
  attachInterrupt(digitalPinToInterrupt(LOGICDATA_RX), logicDataPin_ISR, CHANGE);

  ld.Begin();

  Serial.println("Robodesk v2.5  build: " __DATE__ " " __TIME__);
}

// Record last time the display changed
// sets globals currentHeight and last_signal
void check_display() {
  static uint32_t prev = 0;
  uint32_t msg = ld.ReadTrace();
  char buf[80];
  if (msg) {
    uint32_t now = millis();
    sprintf(buf, "%6ums %s: %s", now - prev, ld.MsgType(msg), ld.Decode(msg));
    Serial.println(buf);
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
        Serial.println("Breaking latches");
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

void move_table_up() {
  // currentHeight is initially 0 before the first move
  if (currentHeight == 0 || isValidHeight(currentHeight)) {
    digitalWrite(ASSERT_UP, HIGH);
    digitalWrite(ASSERT_DOWN, LOW);

    //make sure to only log if there was a change
    if (active == false) {
      active = true;
      mqttClient.publish((MQTT_TOPIC + "active").c_str(), "true");
    }
    if (directionUp == false) {
      directionUp = true;
      mqttClient.publish((MQTT_TOPIC + "direction").c_str(), "up");
    }
  }
}

void move_table_down() {
  // currentHeight is initially 0 before the first move
  if (currentHeight == 0 || isValidHeight(currentHeight)) {
    digitalWrite(ASSERT_DOWN, HIGH);
    digitalWrite(ASSERT_UP, LOW);

    //make sure to only log if there was a change
    if (active == false) {
      active = true;
      mqttClient.publish((MQTT_TOPIC + "active").c_str(), "true");
    }
    if (directionUp == true) {
      directionUp = false;
      mqttClient.publish((MQTT_TOPIC + "direction").c_str(), "down");
    }
  }
}

void move_table() {
  //btn_last_state has current buttons pressed
  if(btn_last_state[0] && btn_last_state[1]) {
    //both buttons pressed, do nothing
    Serial.println("Both buttons pressed");
  } else if(btn_last_state[0]) {
    move_table_up();
    return;
  } else if(btn_last_state[1]) {
    move_table_down();
    return;
  } else if (!latch_up && !latch_down && !setheight) {
    //if not latch, do nothing
    digitalWrite(ASSERT_UP, LOW);
    digitalWrite(ASSERT_DOWN, LOW);
    if (active == true) {
      mqttClient.publish((MQTT_TOPIC + "active").c_str(), "false");
      active = false;
    }
    return;
  }

  if (latch_up && latch_down) {
    Serial.println("Latch up and latch down set, this is an issue");
    while(true) ;
  }

  if((millis() - last_signal > signal_giveup_time) && !setheight) {
    Serial.println("Haven't seen input in a while, turning everything off for safety");
    digitalWrite(ASSERT_UP, LOW);
    digitalWrite(ASSERT_DOWN, LOW);
    latch_up = false;
    latch_down = false;
    while(true) ;
  }

  if(currentHeight != targetHeight) {
    if (latch_up || latch_down) {
      //should be moving
      latch_up ? move_table_up() : move_table_down();
      digitalWrite(!latch_up ? ASSERT_UP : ASSERT_DOWN, LOW);
      return;
    } else if (setheight) {
      if (currentHeight > targetHeight)
        move_table_down();
      else
        move_table_up();
      return;
    }
  } else {
    //hit targetHeight
    digitalWrite(ASSERT_UP, LOW);
    digitalWrite(ASSERT_DOWN, LOW);
    latch_up = false;
    latch_down = false;
    setheight = false;
    directionUp = false;
    Serial.print("Hit targetHeight ");
    Serial.println(targetHeight);
    return;
  }
}

void initMQTT()
{
  if (!mqttClient.connected()) {
      while (!mqttClient.connected()) {
          if (mqttClient.connect(HOSTNAME,
              MQTT_USER, MQTT_PASS,
              (MQTT_TOPIC + "lastConnected").c_str(),
              0,
              true,
              __DATE__ " " __TIME__)) {
            mqttClient.subscribe((MQTT_TOPIC + "set").c_str());
            mqttClient.subscribe((MQTT_TOPIC + "cmd").c_str());
          }
          delay(100);
      }
  } else
    mqttClient.loop();
}

long lastPublish = 0;
byte lastHeight = 0;

void publishHeight() {
    long now = millis();
    if (lastHeight != currentHeight && now - lastPublish > 5000)
    {
        lastPublish = now;
        lastHeight = currentHeight;
        mqttClient.publish((MQTT_TOPIC + "height").c_str(), convertToChar(currentHeight));
    }
}

void loop() {
  // sets global currentHeight and last_signal from logicdata serial
  check_display();

  ArduinoOTA.handle();
  initMQTT();

  if (!setheight) {
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

  move_table();
  publishHeight();
}
