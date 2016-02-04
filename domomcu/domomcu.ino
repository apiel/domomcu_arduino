#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WebServer.h>
//#include <ESP8266WebServerExtend.h> // to implement instead of ESP8266WebServer in order to have parseArguments public method
//#include <ArduinoJson.h>
#include <RCSwitch.h>
#include "FS.h"

typedef struct
{
  const char * uri;
  ESP8266WebServer::THandlerFunction action;
} Routes;

typedef struct
{
  String url;
  bool trigger = false;
} Intterupt;

Intterupt intterupt[3];

typedef struct
{
    String id;
    unsigned long time = 0;
    String actions;
} SleepQueue;

SleepQueue sleepQueue[10];

const char* APssid = "DOMOMCU_01";
const char* APpassword = "domomcuIsGreat";

String startupUrlFile = "/startup_url";
String startupActionsFile = "/startup_actions";
String wifiFile = "/wifi";
String tcpFile = "/tcp";

WiFiClient client;

ESP8266WebServer server(80);

RCSwitch mySwitch = RCSwitch();

void actionsParse(String actions);
void action();
void startupTcp();
void startupUrl();

String urlencode(String str)
{
    String encodedString="";
    char c;
    char code0;
    char code1;
    char code2;
    for (int i =0; i < str.length(); i++){
      c=str.charAt(i);
      if (c == ' '){
        encodedString+= '+';
      } else if (isalnum(c)){
        encodedString+=c;
      } else{
        code1=(c & 0xf)+'0';
        if ((c & 0xf) >9){
            code1=(c & 0xf) - 10 + 'A';
        }
        c=(c>>4)&0xf;
        code0=c+'0';
        if (c > 9){
            code0=c - 10 + 'A';
        }
        code2='\0';
        encodedString+='%';
        encodedString+=code0;
        encodedString+=code1;
        //encodedString+=code2;
      }
      yield();
    }
    return encodedString;   
}

bool isConnectedWifi(void) {
  int test = 20;
  while (test && WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");  
    test--;
  }
  return test;
} 

bool loadWifi() {
  bool connected = false;
  Serial.println("\nSetup Wifi connection.");
  File configFile = SPIFFS.open(wifiFile, "r");
  if (!configFile) {
    Serial.println("Failed to open wifi config file");
  }
  else {
      server.parseArguments(configFile.readString());
      if (server.hasArg("ssid") && server.hasArg("password")) {
        String _ssid = server.arg("ssid");
        String _password = server.arg("password");
        Serial.print("Connect to WiFi: ");  
        Serial.println(_ssid);          
        char ssid[128];
        char password[128];
        _ssid.toCharArray(ssid, 128);
        _password.toCharArray(password, 128);
        WiFi.begin(ssid, password);
        connected = isConnectedWifi();
      }
      else {
        Serial.println("Failed to parse config file");
      }
  }
  if (connected) {
      Serial.println("\nConnected to WiFi.");
      Serial.println(WiFi.localIP());
      startupUrl();
      startupTcp();
  }   
  return connected;
}

bool saveFile(String file, String data) {
  Serial.print("Save file: ");
  Serial.println(file);

  File configFile = SPIFFS.open(file, "w");
  if (!configFile) {
    Serial.println("Failed to open file for writing");
  }
  else {
    configFile.print(data);
  }
  return configFile;  
}

bool saveWifiConfig(String ssid, String password) {
  Serial.println("Save wifi config file.");
  String wifiConfig = "ssid=" + urlencode(ssid) + "&password=" + urlencode(password);
  return saveFile(wifiFile, wifiConfig);
}

void startupTcp() {
  Serial.println("Load startup tcp.");
  File configFile = SPIFFS.open(tcpFile, "r");
  if (!configFile) {
    Serial.println("Failed to open startup tcp config file");
  }
  else {
      String _host = configFile.readString();
      char host[128];
      _host.toCharArray(host, 128);      
      Serial.println(host);

      if(client.available()){
        client.stop();
      }
      if (!client.connect(host, 8888)) {
        Serial.println("connection failed");
      }  
      else {
        Serial.println("connection success");
        client.print("yoyoyo");
      }       
  }
}

void callUrl(String url) {
  Serial.println("Call url");
  Serial.println(url);
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  if(httpCode > 0) {
        Serial.printf("[HTTP] GET... code: %d\n", httpCode);
        if(httpCode == 200) {
            String payload = http.getString();
            Serial.println(payload);
            // here we could execute the payload...
            actionsParse(payload);
        }
  } 
  else {
        Serial.printf("[HTTP] GET... failed, error: %d\n", httpCode);
  }
  http.end();        
}

void startupUrl() {
  Serial.println("Load startup url.");
  File configFile = SPIFFS.open(startupUrlFile, "r");
  if (!configFile) {
    Serial.println("Failed to open startup url config file");
  }
  else {
      String url = configFile.readString();
      callUrl(url);
  }
}

void startupActions() {
  Serial.println("Load startup actions.");
  File configFile = SPIFFS.open(startupActionsFile, "r");
  if (!configFile) {
    Serial.println("Failed to open startup actions config file");
  }
  else {
      String actions = configFile.readString();
      actionsParse(actions);
  }  
}

void loadWifiAP() {
  Serial.println("Setup Wifi access point.");
  WiFi.softAP(APssid, APpassword);
  Serial.println(WiFi.softAPIP());
}

void routeNotFound() {
  Serial.print("Route Not Found ");
  Serial.println(server.uri());
  server.send ( 404, "text/plain", "Not Found!");
}

void routeRoot() {
  Serial.println("Route Root");
  server.send ( 200, "text/plain", "Hello DomoMCU.");
}

void routeWifiConfig() {
  Serial.println("Wifi config");
  if (server.hasArg("ssid") && server.hasArg("password")) {
    saveWifiConfig(server.arg("ssid"), server.arg("password"));
    server.send ( 200, "text/plain", "Wifi Configured.");
  }  
  else {
    server.send ( 400, "text/plain", "Wifi Config parameter missing. Please provide ssid and password.");
  }
}

void routeGpioWrite() {
  Serial.println("Gpio write");
  if (!server.hasArg("pin") || !server.hasArg("value")) {
    server.send(400, "text/plain", "Gpio read parameter missing. Please provide pin and value");    
  }
  else {
    int pin = server.arg("pin").toInt();
    int value = server.arg("value").toInt();
    pinMode(pin, OUTPUT);
    digitalWrite(pin, value);
    server.send(200, "text/plain", "DONE");
  }
}

void intterruptCallback0() {
  Serial.println("intterupt 0");
  intterupt[0].trigger = true;
}

void intterruptCallback1() {
  Serial.println("intterupt 1");
  intterupt[1].trigger = true;
}

void intterruptCallback2() {
  Serial.println("intterupt 2");
  intterupt[2].trigger = true;
}

void routeAttachInterrupt() {
  Serial.println("Attach Interrupt");
  if (!server.hasArg("pin")) {
    server.send(400, "text/plain", "Attach Interrupt parameter missing. Please provide pin");    
  }
  else {
    int pin = server.arg("pin").toInt();
    if (!server.hasArg("url")) {
      Serial.println("Detach Interrupt");
      detachInterrupt(pin);
    }
    else {
      int mode = CHANGE;
      if (server.hasArg("mode")) {
        if (server.arg("mode").equals("RISING")) {
          mode = RISING;
        }
        else if (server.arg("mode").equals("FALLING")) {
          mode = FALLING ;
        }
      }
      pinMode(pin, INPUT);
      if (server.hasArg("callback")) {
        if (server.arg("callback").equals("2")) {
          intterupt[2].url = server.arg("url");
          attachInterrupt(pin, intterruptCallback2, mode);
        }
        else if (server.arg("callback").equals("1")) {
          intterupt[1].url = server.arg("url");
          attachInterrupt(pin, intterruptCallback1, mode);
        }
      }
      else {
        intterupt[0].url = server.arg("url");
        attachInterrupt(pin, intterruptCallback0, mode);
      }
      server.send(200, "text/plain", "DONE");          
    }
  }
}

void routeGpioRead() {
  Serial.println("Gpio read");
  if (!server.hasArg("pin")) {
    server.send(400, "text/plain", "Gpio read parameter missing. Please provide pin");    
  }
  else {
    int pin = server.arg("pin").toInt();
    pinMode(pin, INPUT);
    int val = digitalRead(pin);
    server.send(200, "text/plain", String(val));
  }
}

void routeAnalogRead() {
  Serial.println("Analog read");
  pinMode(A0, INPUT);
  int val = analogRead(A0);
  server.send(200, "text/plain", String(val));
}

void routeRcswitchSend() {
  Serial.println("Rcswitch send");
  if (server.hasArg("pin") && server.hasArg("code")) {
    mySwitch.enableTransmit(server.arg("pin").toInt());
    mySwitch.setProtocol(server.hasArg("protocol") ? server.arg("protocol").toInt() : 1);
    mySwitch.setPulseLength(server.hasArg("pulse") ? server.arg("pulse").toInt() : 180);
    mySwitch.setRepeatTransmit(server.hasArg("repeat") ? server.arg("repeat").toInt() : 15);
    if (server.hasArg("bit")) {
      mySwitch.send(server.arg("code").toInt(), server.arg("bit").toInt());
    }
    else {
      char code[128];
      server.arg("code").toCharArray(code, 128);
      mySwitch.send(code);
    }
    server.send(200, "text/plain", "Code sent.");
  }
  else {
    server.send(400, "text/plain", "Rcswitch send parameter missing. Please provide pin and code");
  }
}

void routeStartupUrl() {
  Serial.println("Startup Url");
  if (!server.hasArg("url")) {
    server.send(400, "text/plain", "Startup url parameter missing. Please provide url");
  }  
  else {
    String url = server.arg("url");
    Serial.println(url);
    saveFile(startupUrlFile, url);
    server.send(200, "text/plain", "DONE");
  }
}

void routeUpdate() {
  Serial.println("Update");
  if (!server.hasArg("url")) {
    server.send(400, "text/plain", "Update parameter missing. Please provide url");
  }  
  else {
    String url = server.arg("url");
    Serial.println(url);
    char * _url;
    url.toCharArray(_url, url.length());
    if (ESPhttpUpdate.update(_url) == HTTP_UPDATE_OK) {
      server.send(200, "text/plain", "DONE");
    }
    else {
      server.send(200, "text/plain", "FAILED");
    }
  }  
}

void routeStartupActions() {
  Serial.println("Startup Actions");
  if (!server.hasArg("actions")) {
    server.send(400, "text/plain", "Startup actions parameter missing. Please provide actions");
  }  
  else {
    String actions = server.arg("actions");
    saveFile(startupActionsFile, actions);
    server.send(200, "text/plain", "DONE");
  }
}
void loopTriggerIntterupt() {
  for (int i = 0; i < 3; i++){
    if (intterupt[i].trigger) {
      intterupt[i].trigger = false;
      Serial.print("Trigger: ");
      Serial.println(intterupt[i].url);    
      callUrl(intterupt[i].url);
    }
  }
}

void triggerSleep() {
  int count = sizeof(sleepQueue)/sizeof(sleepQueue[0]);
  for (int i = 0; i < count; i++){
      if (sleepQueue[i].time != 0 && sleepQueue[i].time < millis()) {
        sleepQueue[i].time = 0;
        Serial.println("Do sleep action");
        actionsParse(sleepQueue[i].actions);
      }
  }
}

bool addSleep(String id, unsigned long time, String actions) {
  int count = sizeof(sleepQueue)/sizeof(sleepQueue[0]);
  int addTo = -1;
  if (id != "") {
    for (int i = 0; i < count; i++){
      if (id == sleepQueue[i].id) {
        addTo = i;
        break;
      }
    }  
  }
  if (addTo == -1) {
    for (int i = 0; i < count; i++){
      if (sleepQueue[i].time == 0) {
        addTo = i;
        break;
      }
    }
  }
  if (addTo != -1) {
    sleepQueue[addTo].id = id;
    sleepQueue[addTo].time = time;
    sleepQueue[addTo].actions = actions;
  }    
  return addTo != -1;
}

void routeSleep() {
  Serial.println("Route sleep");
  if (!server.hasArg("actions") || !server.hasArg("millisec")) {
    server.send(400, "text/plain", "Sleep parameter missing. Please provide actions and millisec");
  }  
  else {
    String id = server.hasArg("id") ? "" : server.arg("id");
    if (addSleep(id, millis()+server.arg("millisec").toInt(), server.arg("actions"))) {
      server.send(200, "text/plain", "DONE");  
    }
    else {
      server.send(200, "text/plain", "SLEEP QUEUE FULL");
    }
  }  
}

void routeTcp() {
  Serial.println("Route tcp");
  if (!server.hasArg("host")) {
    server.send(400, "text/plain", "Tcp parameter missing. Please provide host");
  }
  else {
    String host = server.arg("host");
    Serial.println(host);
    saveFile(tcpFile, host);
    server.send(200, "text/plain", "DONE");
  }
  startupTcp();
}

void actionsParse(String actions) {
  int startPos = 0;
  int tokenPos;

  do{
    tokenPos = actions.indexOf("\n", startPos);
    String queryAction = actions.substring(startPos, tokenPos);
    startPos = tokenPos + 1;
    Serial.print("Action: ");
    Serial.println(queryAction);
    server.parseArguments(queryAction);
    action();    
  } while (tokenPos != -1 && startPos < actions.length());
}

Routes routes[11] = {
  {"/wifi/config", routeWifiConfig},
  {"/rcswitch/send", routeRcswitchSend},
  {"/gpio/read", routeGpioRead},
  {"/gpio/write", routeGpioWrite},
  {"/analog/read", routeAnalogRead},
  {"/startup/url", routeStartupUrl},
  {"/attach/interrupt", routeAttachInterrupt},
  {"/startup/actions", routeStartupActions},
  {"/update", routeUpdate},
  {"/sleep", routeSleep},
  {"/tcp", routeTcp}
};


void action(){
  if (server.hasArg("action")){
    int count = sizeof(routes)/sizeof(routes[0]);
    String action = server.arg("action");
    for (int i = 0; i < count; i++){
      if (action.equals(routes[i].uri)) {
        routes[i].action();
      }
    }    
  }
}

void setupServerRoutes(){
  int count = sizeof(routes)/sizeof(routes[0]);
  for (int i = 0; i < count; i++){
    server.on(routes[i].uri, routes[i].action);
  }
}

void setup() {  
  Serial.begin(115200);
  
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
  }  

  startupActions();
  
  //saveWifiConfig("UPC0042232", "QPJUIISS");
  if (!loadWifi()) {
      Serial.println("\nCan\'t connect to WiFi, run access point.");
      loadWifiAP();
  }

  server.on("/", routeRoot);
  setupServerRoutes();
  server.onNotFound(routeNotFound);
  server.begin();
  Serial.println("HTTP server started");  

  //startupUrl();

  //actionsParse("action=/rcswitch/send&pin=5&code=283955&bit=24\naction=/rcswitch/send&pin=5&code=283964&bit=24\naction=/rcswitch/send&pin=5&code=283955&bit=24\naction=/rcswitch/send&pin=5&code=283964&bit=24\n");  
}

void loop() {
  server.handleClient();
  loopTriggerIntterupt();
  triggerSleep();

  if(client.available()){
    String line = client.readStringUntil('\r');
    Serial.print(line);
    actionsParse(line);
  }
}
