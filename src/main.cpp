/* ------------- OUTLINE ------------------
1. Include statements
2. Wireless credentials
3. Mqtt credentials
5. IR configuations
4. Pin configurations
5. Constructors
6. Setup
7. Loop
8. Functions
*/ 

// libraries
  // standard i/o libraries
    #include <Arduino.h>
    #include <esp_log.h>
    #include "soc/sens_reg.h" // needed for manipulating ADC2 control register
  // wireless libraries
    #include <WiFi.h>
    #include <WifiCredentials.h>
    #include <DNSServer.h>
    #include <WebServer.h>
    #include <ArduinoOTA.h>
    #include <WiFiManager.h>
    #include <PubSubClient.h>
  // project specific
    #include <IRremoteESP8266.h>
    #include <IRSend.h>
    #include <IRrecv.h>
    #include <IRutils.h>
    #include <ArduinoJSON.h>
// end libraries

// set debug level
#define DEBUG_LEVEL ESP_LOG_VERBOSE

// WiFi constants
//  const char * ssid = "SSID";
//  const char * passwd = "PASSWORD";

// MQTT constants
  const char * mqtt_ID = "TVswitch";
  const char * mqtt_server = "192.168.1.4";
  uint16_t mqtt_port = 1883;
  const char * outTopic = "TVswitch/OUT";
  const char * inTopic = "TVswitch/IN";
  const char * cmndTopic = "TVswitch/CMND"; // default is "TVswitch/CMND"
  const char * statTopic = "TVswitch/STAT"; // default is "TVswitch/STAT"
  const char * teleTopic = "TVswitch/TELE"; // default is "TVswitch/TELE"

// IR values
  // all codes are NEC protocol
  const uint64_t plusOneCode = 0x40BF7B84;
  const uint64_t minusOneCode = 0x40BFAA55;
  const uint64_t input1Code = 0x40BFFB04;
  const uint64_t input2Code = 0x40BFF906;
  const uint64_t input3Code = 0x40BFC33C;
  const uint64_t input4Code = 0x40BF19E6;
  const uint64_t input5Code = 0x40BFE916;

// gpio pin assignments
  const uint8_t buttonPin = 22;
  const uint8_t irPin = 21;

// initialise global variables
  uint8_t activeInput;
  uint8_t desiredInput;
  uint64_t reg_b; // Used to store ADC2 control register
  StaticJsonDocument<256> doc;
  decode_results results;

// instance creation
  ArduinoOTAClass OTA;
  WiFiClient wiFiClient;
  PubSubClient mqttClient;
  IRsend irsend(irPin);

// constructors
  void wifiSetup();
  void mqttSetup();
  void mqttReconnect();
  void callback(const char* topic, uint8_t * payload, unsigned int length);
  void ioSetup();
  void changeInput(uint8_t desiredInput);

void setup(){
  esp_log_level_set("*", DEBUG_LEVEL);
  Serial.begin(115200);
  wifiSetup();
  mqttSetup();
  ioSetup();
}

void loop(){
  // main stuff happens on mqtt message recieved


  // finally, reconnect if necessary
  while (WiFi.status() != WL_CONNECTED){
    ESP_LOGI(wifi, "RECONNECTING TO WIFI");
    delay(500);
  }
  if (mqttClient.connected()){
    ESP_LOGI(mqtt, "RECONNECTING TO MQTT");
    mqttReconnect();
  }

  OTA.handle();
  delay(10000);
}


// setup functions
  // wifiManager
    //void wifiSetup() {
    //     ESP_LOGI(wifi, "Using WiFi manager");
    //     // Serial.println("Using WiFi manager");
    //     //Local intialization. Once its business is done, there is no need to keep it around
    //       WiFiManager wifiManager;
        
    //     //reset settings - for testing
    //     //wifiManager.resetSettings();
    //     /*sets timeout until configuration portal gets turned off
    //     useful to make it all retry or go to sleep
    //     in seconds */
    //       wifiManager.setTimeout(180);
        
    //     /*fetches ssid and pass and tries to connect
    //     if it does not connect it starts an access point with the specified name
    //     here  "AutoConnectAP" (wifiManager.autoConnect("AP-NAME", "AP-PASSWORD"))
    //     and goes into a blocking loop awaiting configuration */
    //       if(!wifiManager.autoConnect()) {
    //         ESP_LOGE(wifi, "Failed to connect and hit timeout. Resetting in 3 seconds");
    //         // Serial.println("failed to connect and hit timeout");
    //         delay(3000);
    //         //reset and try again, or maybe put it to deep sleep
    //         esp_restart();
    //         delay(5000);
    //       }
    //     ESP_LOGI(wifi, "WiFiManager setup complete");
    //     // Serial.println("WiFiManager setup complete");
    // }

  // manual wifi setup commented out
    void wifiSetup(){
      WiFi.begin(ssid, passwd);
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        ESP_LOGV(wifi, "Connecting to WiFi..");
      }
      ESP_LOGV(wifi, "Connected to wifi");
    }

    void mqttSetup(){
        mqttClient.setClient(wiFiClient);
        mqttClient.setServer(mqtt_server, mqtt_port);
        mqttClient.setCallback(callback);

        while (mqttClient.connect(mqtt_ID) == false) {
          mqttClient.connect(mqtt_ID);
        }

        mqttClient.subscribe(inTopic);
        mqttClient.subscribe(cmndTopic);
        mqttClient.subscribe(statTopic);
        mqttClient.subscribe(teleTopic);
    };

  // PubSubClient
    void callback(const char* topic, uint8_t * payload, unsigned int length) {
      ESP_LOGD(mqtt, "Message arrived in topic [%s]", topic);
      ESP_LOGD(mqtt, "Payload = %s", payload);
      mqttClient.publish(outTopic, "Message recieved");

      // parse the JSON input
      deserializeJson(doc, payload, length);
      uint8_t desiredInput = doc["desiredInput"];

      // if necessary, call the changeInput function
      if (activeInput != desiredInput){
        ESP_LOGV(general, "changeInput called with input %i", desiredInput);
        changeInput(desiredInput);
      }
    }

    void mqttReconnect() {
      // Loop until we're reconnected
      while (!mqttClient.connected()) {
        // Serial.print("Attempting MQTT connection...");
        ESP_LOGI(mqtt, "Attempting MQTT connection...");

        // Attempt to connect
        if (mqttClient.connect(mqtt_ID)) {
          // Serial.println("connected");
          ESP_LOGI(mqtt, "Connected");

          // Once connected, publish an announcement...
          mqttClient.publish(outTopic, "TV switch online");

          // ... and resubscribe
          mqttClient.subscribe(inTopic);
        } else {
          // Serial.print("failed, rc=");
          // Serial.print(mqttClient.state());
          // Serial.println(" try again in 5 seconds");
          ESP_LOGE(mqtt, "Failed, rc= %i. Try again in 5 seconds.");
          
          // Wait 5 seconds before retrying
          delay(5000);
        }
      }
    }

void ioSetup(){

  irsend.begin();
  pinMode(buttonPin, INPUT);
}

void changeInput(uint8_t desiredInput){
  switch (desiredInput){
    case 1:
      irsend.sendNEC(input1Code);
      break;
    case 2:
      irsend.sendNEC(input2Code);
      break;
    case 3:
      irsend.sendNEC(input3Code);
      break;
    case 4:
      irsend.sendNEC(input4Code);
      break;
    case 5:
      irsend.sendNEC(input5Code);
      break;
      
    default:
      break;
  }
}