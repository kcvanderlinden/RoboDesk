#include <pins.h>
#include <LogicData.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
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
void ICACHE_RAM_ATTR logicDataPin_ISR() {
  ld.PinChange(HIGH == digitalRead(LOGICDATA_RX));
}

uint8_t highTarget = 41;
uint8_t lowTarget = 28;

uint8_t height;
uint8_t target;

void setup_wifi() {
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    #ifdef WIFICONFIG_H
      WiFi.config(WIFI_IP, WIFI_DNS, WIFI_GATEWAY, WIFI_SUBNET);
    #endif
    WiFi.setAutoReconnect(true);
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
}

void setup() {

  pinMode(BTN_UP, INPUT);
  pinMode(BTN_DOWN, INPUT);
  pinMode(LOGICDATA_RX, INPUT);

  pinMode(ASSERT_UP, OUTPUT);
  pinMode(ASSERT_DOWN, OUTPUT);

  Serial.begin(115200);

  setup_wifi();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  logicDataPin_ISR();
  attachInterrupt(digitalPinToInterrupt(LOGICDATA_RX), logicDataPin_ISR, CHANGE);

  ld.Begin();

  Serial.println("Robodesk v2.x  build: " __DATE__ " " __TIME__);
}

// Record last time the display changed
// sets globals height and last_signal
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
    if (new_height == height) {
      return;
    }
    height = new_height;
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
      target = highTarget;
      Serial.print("Latching UP ");
      Serial.println(target);
      break;
    case DownDouble:
      latch_down = true;
      target = lowTarget;
      Serial.print("Latching Down ");
      Serial.println(target);
      break;
  }
}

void move() {
  //btn_last_state has current buttons pressed
  if(btn_last_state[0] && btn_last_state[1]) {
    //both buttons pressed, do nothing
    //do nothing
  } else if(btn_last_state[0]) {
    digitalWrite(ASSERT_UP, HIGH);
    return;
  } else if(btn_last_state[1]) {
    digitalWrite(ASSERT_DOWN, HIGH);
    return;
  } else if (!latch_up && !latch_down) {
    //if not latch, do nothing
    digitalWrite(ASSERT_UP, LOW);
    digitalWrite(ASSERT_DOWN, LOW);
    return;
  }

  if (latch_up && latch_down) {
    Serial.println("Latch up and latch down set, this is an issue");
    while(true) ;
  }
  if(millis() - last_signal > signal_giveup_time) {
    Serial.println("Haven't seen input in a while, turning everything off for safety");
    digitalWrite(ASSERT_UP, LOW);
    digitalWrite(ASSERT_DOWN, LOW);
    latch_up = false;
    latch_down = false;
    while(true) ;
  }

  if(height != target) {
    //should be moving
    digitalWrite(latch_up ? ASSERT_UP : ASSERT_DOWN, HIGH);
    digitalWrite(!latch_up ? ASSERT_UP : ASSERT_DOWN, LOW);
    return;
  } else {
    //hit target
    digitalWrite(ASSERT_UP, LOW);
    digitalWrite(ASSERT_DOWN, LOW);
    latch_up = false;
    latch_down = false;
    Serial.print("Hit target ");
    Serial.println(target);
    return;
  }
}


void loop() {
  // sets global height and last_signal from logicdata serial
  check_display();

  if (!mqttClient.connected()) {
      while (!mqttClient.connected()) {
          mqttClient.connect("ESP8266Client", MQTT_USER, MQTT_PASS);
          delay(100);
      }
  } else {
    mqttClient.publish((MQTT_TOPIC + "state").c_str(), "connected");
  }
  mqttClient.loop();

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
  move();
}
