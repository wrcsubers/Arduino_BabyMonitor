//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Arduino_BabyMonitor distribution (https://github.com/wrcsubers/Arduino_BabyMonitor).
// Copyright (c) 2021 wrcsubers - Cameron W.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 3.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Libraries
// ========================================================================
#include <Arduino.h>
#include <Hash.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>
#include <CircularBuffer.h>


// Webserver & Websocket Settings
// ========================================================================
ESP8266WebServer webServer(80);                     // Start WebServer at Port 80
WebSocketsServer webSocket = WebSocketsServer(81);  // Start WebSocket at Port 81


// Network Settings
// ========================================================================
const char* WIFI_SSID = "YourWiFiNetwork";                  // Your WiFi Network Name
const char* WIFI_Password = "YourWiFiPassword";        // Your WiFi Network Password
const char* mDNS_Hostname = "BabyMonitor";           // Local Multicast-DNS Hostname - Don't add '.local' to the end


// Pin Settings
// ========================================================================
const uint8_t StatusLED_Pin = D0;         // Pin to show if board is running (inverted) - NodeMCU LED near USB Port
const uint8_t SoundLED_Pin = D4;          // Pin to show that audio is above threshold (inverted) - ESP8266 LED near antenna
const uint8_t Mic_Pin = A0;               // Mic Input from OpAmp - Must be Analog


// Sound Level & Microphone Settings
// ========================================================================
const uint8_t micSampleDelay = 50;        // How long between mic readings (ms) - Too small of a delay will cause websocket issues! - Default is 50

uint8_t soundLowAlertLevel = 15;          // Sound Threshold Levels are the triggers which define when to alert clients.
uint8_t soundMidAlertLevel = 50;          // These can be changed via the web interface.
uint8_t soundHighAlertLevel = 100;        // Default is 15, 50, 100

CircularBuffer<int, 50> micDeltaBuffer;   // Create circular buffer of ints, holding 50 values.  Used for smoothing input

uint16_t rawMicValue;
uint16_t micMinValue = 1024;
uint16_t micMaxValue = 0;
uint16_t micDeltaValue = 0;
uint16_t micDeltaAverage = 0;

uint8_t micDeltaSamplesToAvg = 10;        // Number of mic readings to average together - Can be changed via web interface.

uint8_t micReadingCurrent = 0;
uint32_t micTimeStart;
uint32_t micTimeNow;
uint32_t micTimeElapsed;


// Websocket Update Delay Timer
// ========================================================================
const uint8_t updateTimeDelay = 100;      // How long to wait for other updates to process (ms) - Default is 100

uint32_t updateTimeStart;
uint32_t updateTimeNow;
uint32_t updateTimeElapsed;
bool updateNeeded = false;


// Other Settings
// ========================================================================
bool systemOkay = false;
bool nightMode = false;




// Function for handling WebSocket Events
// ========================================================================
void webSocketReceive(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    // Websocket Disconnected State
    // ---------------------------------------------------------------------------
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    // Websocket Connected State
    // ---------------------------------------------------------------------------
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        updateClients();               // When a client connects, send all current values to clients
      }
      break;
    // Websocket Receives New Message
    // ---------------------------------------------------------------------------
    case WStype_TEXT:
      Serial.printf("Message Received: %s\n", payload);
      char *stringToSplit = (char*) payload;
      const char delim[4] = "_|_";                      // _|_ is used to delimit strings incoming/outgoing
      char *msgType = strtok(stringToSplit, delim);     // First part of payload is the msgType (variable)
      char *msgData = strtok(NULL, delim);              // Second part is the msgData (variable value)
      if ((msgType != NULL) && (msgData != NULL)) {     // Check if message contains two parts
        processWebSocketMessage(msgType, msgData);      // Send to processing function
      } else {
        Serial.println("Message is malformed - Unable to process!");
      }
      break;
  }
}




// Function for incoming WebSocket Messages
// ========================================================================
void processWebSocketMessage(char * msgType, char * msgData) {
  if (strcmp(msgType, "nightMode") == 0) {
    nightMode = atoi(msgData);
  } else if (strcmp(msgType, "rangeLowAlert") == 0) {
    soundLowAlertLevel = atoi(msgData);
  } else if (strcmp(msgType, "rangeMidAlert") == 0) {
    soundMidAlertLevel = atoi(msgData);
  } else if (strcmp(msgType, "rangeHighAlert") == 0) {
    soundHighAlertLevel = atoi(msgData);
  } else if (strcmp(msgType, "rangeMicSamplesToAverage") == 0) {
    micDeltaSamplesToAvg = atoi(msgData);
  } else {
    Serial.println("Message not Found!");
    return;
  }
  // Set update flag and start timer - prevents excessive updates
  // ---------------------------------------------------------------------------
  updateNeeded = true;
  updateTimeStart = millis();
}




// Function to update clients
// ========================================================================
void updateClients() {
  webSocketSend("nightMode", String(nightMode));
  delay(20);
  webSocketSend("rangeLowAlert", String(soundLowAlertLevel));
  delay(20);
  webSocketSend("rangeMidAlert", String(soundMidAlertLevel));
  delay(20);
  webSocketSend("rangeHighAlert", String(soundHighAlertLevel));
  delay(20);
  webSocketSend("rangeMicSamplesToAverage", String(micDeltaSamplesToAvg));
}




// Function for outgoing WebSocket Messages
// ========================================================================
// Use: webSocketSend("Type", String(Data));
void webSocketSend(String msgType, String msgData) {
  String delimiter = "_|_";
  webSocket.broadcastTXT(msgType + delimiter + msgData);
}




// Function to determine file type for webServer
// ========================================================================
String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}




// Function to read/stream files from flash
// ========================================================================
bool handleFileRead(String httpPathRequested) {
  Serial.println("handleFileRead: " + httpPathRequested);
  if (httpPathRequested.endsWith("/")) {            // Send default HTML file on request at just domainname/IP
    httpPathRequested = "BabyMonitor.html";
  }
  String contentType = getContentType(httpPathRequested);
  if (LittleFS.exists(httpPathRequested)) {
    File file = LittleFS.open(httpPathRequested, "r");
    size_t sent = webServer.streamFile(file, contentType);
    file.close();
    return true;
  }
  Serial.println("\tFile Not Found");
  return false;
}



// Setup Function
// ========================================================================
void setup() {
  // Start Serial
  // ---------------------------------------------------------------------------
  Serial.begin(9600);

  // Setup Inputs/Outputs/Interrupts
  // ---------------------------------------------------------------------------
  pinMode(StatusLED_Pin, OUTPUT);
  pinMode(SoundLED_Pin, OUTPUT);
  pinMode(Mic_Pin, INPUT);

  // Turn off Status and Sound LEDs
  // ---------------------------------------------------------------------------
  digitalWrite(StatusLED_Pin, HIGH);
  digitalWrite(SoundLED_Pin, HIGH);

  // Connect to WiFi
  // ---------------------------------------------------------------------------
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_Password);
  Serial.print("\nWaiting for WiFi to connect...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    digitalWrite(StatusLED_Pin, LOW);
    delay(150);
    digitalWrite(StatusLED_Pin, HIGH);
    delay(150);
  }
  digitalWrite(StatusLED_Pin, HIGH);
  Serial.println("\n\nWiFi connected! IP Address: " + (WiFi.localIP()).toString());

  // Start mDNS Service
  // ---------------------------------------------------------------------------
  if (!MDNS.begin(mDNS_Hostname, WiFi.localIP())) {
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.println("mDNS started with hostname: " + String(mDNS_Hostname));
  MDNS.addService("http", "tcp", 80);

  // Start File System
  // ---------------------------------------------------------------------------
  LittleFS.begin();

  // Create a script and add URL for websocket connection based on current IP Address
  // ---------------------------------------------------------------------------
  File textIPFile = LittleFS.open("wsConnection.js", "w");
  textIPFile.print("wsConnection = \"ws://" + WiFi.localIP().toString() + ":81\";\n");
  textIPFile.close();

  // Configure and Start webServer
  // ---------------------------------------------------------------------------
  // If client requests any URI, send it, otherwise respond with 404
  webServer.onNotFound([]() {
    if (!handleFileRead(webServer.uri()))
      webServer.send(404, "text/plain", "404: Not Found");
  });

  webServer.begin();
  Serial.println("HTTP server started");

  // Setup WebSocket Stuff
  // ---------------------------------------------------------------------------
  webSocket.begin();
  webSocket.onEvent(webSocketReceive);

  // Initialize mic reading timer
  // ---------------------------------------------------------------------------
  micTimeStart = millis();

  //End Setup
  // ---------------------------------------------------------------------------
  systemOkay = true;
  Serial.println("Setup Complete =)");
}




// Loop Function
// ========================================================================
void loop() {

  // Sample Mic Value according to timer
  // ---------------------------------------------------------------------------
  micTimeNow = millis();
  micTimeElapsed = micTimeNow - micTimeStart;
  if (micTimeElapsed >= micSampleDelay) {

    rawMicValue = analogRead(Mic_Pin);               // Read Mic Value after delay

    micReadingCurrent++;                             // Increment Counter for each read

    micMinValue = min(micMinValue, rawMicValue);     // Change min value if new value is less
    micMaxValue = max(micMaxValue, rawMicValue);     // Change max value if new value is higher

    if (micReadingCurrent >= 4) {
      micDeltaValue = (micMaxValue - micMinValue);           // Calculate Delta (difference between softest & loudest sounds
      micDeltaBuffer.unshift(micDeltaValue);                 // Add Delta Value to buffer, which will push old values out
      for (int i = 0; i < micDeltaSamplesToAvg; i++) {       // Grab number of values from array based on micDeltaSamplesToAvg variable
        micDeltaAverage += micDeltaBuffer[i];                // Add just those values to the average
      }
      micDeltaAverage = (micDeltaAverage / micDeltaSamplesToAvg);   // Finally, compute the average
      webSocketSend("micDeltaAverage", String(micDeltaAverage));    // Send micDeltaAverage to clients

      micMinValue = 1024;                // Reset Values
      micMaxValue = 0;                   //
      micDeltaAverage = 0;               //

      micReadingCurrent = 0;             // Reset Counter
    }
    micTimeStart = millis();             // Reset Timer
  }


  // Function to delay Websocket Updates after status change
  // ---------------------------------------------------------------------------
  if (updateNeeded == true) {
    updateTimeNow = millis();
    updateTimeElapsed = updateTimeNow - updateTimeStart;
    if (updateTimeElapsed >= updateTimeDelay) {
      updateNeeded = false;
      updateClients();
    }
  }


  // Toggle Built-in Lights On/Off
  // ---------------------------------------------------------------------------
  // Turn on/off built in NodeMCU LED depending on status (LED is inverted)
  if ((systemOkay == true) && (nightMode == false)) {
    digitalWrite(StatusLED_Pin, LOW);
  } else {
    digitalWrite(StatusLED_Pin, HIGH);
  }

  // Turn on/off built in ESP8266 LED if noise is above/below threshold value (LED is inverted)
  if ((micDeltaValue >= soundLowAlertLevel) && (nightMode == false)) {
    digitalWrite(SoundLED_Pin, LOW);
  } else {
    digitalWrite(SoundLED_Pin, HIGH);
  }


  //Check for WebSocket Stuff
  // ---------------------------------------------------------------------------
  webSocket.loop();

  //Check for Webserver Stuff
  // ---------------------------------------------------------------------------
  webServer.handleClient();

  //Check for MDNS Stuff
  // ---------------------------------------------------------------------------
  MDNS.update();
}
