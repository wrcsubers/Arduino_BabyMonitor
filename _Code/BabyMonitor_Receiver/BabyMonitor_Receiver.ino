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
#include <WebSocketsClient.h>


// Websocket Settings
// ========================================================================
WebSocketsClient webSocket;


// Network Settings
// ========================================================================
const char* WIFI_SSID = "YourWiFiNetwork";
const char* WIFI_Password = "YourWiFiPassword";

// mDNS name of the transmitter you want to connect to - don't forget to add .local
const char* transmitterHostname = "BabyMonitor.local";


// Pin Settings
// ========================================================================
const uint8_t StatusLED_Pin = D0;           // Pin to show if board is running (inverted) - NodeMCU LED near USB Port
const uint8_t SyncLED_Pin = D4;             // Pin to show connection to server is good (inverted) - ESP8266 LED near antenna
const uint8_t SoundLevel1_Pin = D1;         // Pin to show low sound level
const uint8_t SoundLevel2_Pin = D2;         // Pin to show medium sound level
const uint8_t SoundLevel3_Pin = D3;         // Pin to show high sound level
const uint8_t BuzzerOutput_Pin = D5;        // Pin to buzzer +
const uint8_t MuteLED_Pin = D6;             // Pin to show when alarm is muted
const uint8_t MuteButton_Pin = D7;          // Input for Mute Button (Pulled Low to activate)


// Alarm Settings
// ========================================================================
// Alarm Duration - Calculated as: ((alarmDelayDuration / 2) * maxAlarmCycles) / 1000 = Output Duration - eg. (50 * 100) / 1000 = 2.5, so 10 cycles per second for 2.5 seconds
const unsigned long alarmDelayDuration = 40;         // Time between beeps
const int maxAlarmCycles = 100;                      // Max number of times to beep

int alarmCycles = 0;
unsigned long alarmTimeStart;
unsigned long alarmTimeNow;
unsigned long alarmTimeElapsed;


// Muting Settings
// ========================================================================
const unsigned long autoUnmuteDuration = 300000;       // The timeout for auto unmute to activate in ms (default is 5min)

bool isMuted = false;
bool muteStateChange = false;
bool soundAlarm = false;
bool autoUnmuteActive = false;
unsigned long autoUnmuteTimeStart;
unsigned long autoUnmuteTimeNow;
unsigned long autoUnmuteTimeElapsed;


// Sound Level Settings
// ========================================================================
// These are the triggers which define alerts.  These can be changed via the web interface.
uint8_t soundLowAlertLevel = 100;          //
uint8_t soundMidAlertLevel = 100;          //  All 3 values are retrieved from server on startup.
uint8_t soundHighAlertLevel = 100;         //

uint16_t micDeltaAverage = 0;


// Button De-bounce Settings
// ========================================================================
const unsigned long debounceDelay = 200;     // How long to debounce button for
volatile bool debouncing = false;
unsigned long debounceStart;


// Other Settings
// ========================================================================
bool systemOkay = false;
bool syncOkay = false;
bool nightMode = false;




// Function for handling WebSocket Events
// ========================================================================
void webSocketOnEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    // Websocket Disconnected State
    // --------------------------------------------------------------------------
    case WStype_DISCONNECTED: {
        syncOkay = false;
        micDeltaAverage = 0;  // Prevents stuck alarm
        Serial.printf("[WSc] Disconnected!\n");
      }
      break;
    // Websocket Connected State
    // ---------------------------------------------------------------------------
    case WStype_CONNECTED: {
        syncOkay = true;
        Serial.printf("[WSc] Connected to url: %s\n", payload);
      }
      break;
    // Websocket Receives New Message
    // ---------------------------------------------------------------------------
    case WStype_TEXT:
      Serial.printf("Message Received: %s\n", payload);
      char *stringToSplit = (char*) payload;
      const char delim[4] = "_|_";
      char *msgType = strtok(stringToSplit, delim);     // First part of message is the msgType (variable)
      char *msgData = strtok(NULL, delim);              // Second part of message is the msgData (variable value)
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
  if (strcmp(msgType, "micDeltaAverage") == 0) {
    micDeltaAverage = atoi(msgData);
  } else if (strcmp(msgType, "nightMode") == 0) {
    nightMode = atoi(msgData);
  } else if (strcmp(msgType, "rangeLowAlert") == 0) {
    soundLowAlertLevel = atoi(msgData);
  } else if (strcmp(msgType, "rangeMidAlert") == 0) {
    soundMidAlertLevel = atoi(msgData);
  } else if (strcmp(msgType, "rangeHighAlert") == 0) {
    soundHighAlertLevel = atoi(msgData);
  } else {
    Serial.println("Message not Found!");
    return;
  }
}




// Interrupt Service Request Function for Mute Button
// ========================================================================
void ICACHE_RAM_ATTR toggleMute () {
  if (debouncing == false) {
    debouncing = true;
    debounceStart = millis();
    muteStateChange = true;
  }
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
  pinMode(SyncLED_Pin, OUTPUT);
  pinMode(SoundLevel1_Pin, OUTPUT);
  pinMode(SoundLevel2_Pin, OUTPUT);
  pinMode(SoundLevel3_Pin, OUTPUT);
  pinMode(BuzzerOutput_Pin, OUTPUT);
  pinMode(MuteLED_Pin, OUTPUT);
  pinMode(MuteButton_Pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(MuteButton_Pin), toggleMute, FALLING);

  // Turn off Status and Sync LEDs
  // ---------------------------------------------------------------------------
  digitalWrite(StatusLED_Pin, HIGH);
  digitalWrite(SyncLED_Pin, HIGH);

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

  // Setup WebSocket Stuff
  // ---------------------------------------------------------------------------
  webSocket.begin(transmitterHostname, 81, "/");
  webSocket.onEvent(webSocketOnEvent);
  webSocket.setReconnectInterval(5000);
  webSocket.enableHeartbeat(15000, 3000, 2);

  // End Setup
  // ---------------------------------------------------------------------------
  systemOkay = true;
  Serial.println("Setup Complete =)");
}


// Loop Function
// ========================================================================
void loop() {

  // Button Debounce Logic
  // ---------------------------------------------------------------------------
  if (debouncing == true) {
    if ((millis() - debounceStart) > debounceDelay) {
      debouncing = false;
    }
  }

  // Mute Toggling - muteStateChange is toggled by ISR
  // ---------------------------------------------------------------------------
  if (muteStateChange == true) {
    if (isMuted == true) {                  // Unmute on toggle, turn off autoUnmute and turn off LED indicator
      isMuted = false;
      autoUnmuteActive = false;
      digitalWrite(MuteLED_Pin, LOW);
    } else {                                // Mute on toggle, turn on autoUnmute, start autoUnmute Timer and turn on LED indicator
      isMuted = true;
      autoUnmuteActive = true;
      autoUnmuteTimeStart = millis();
      digitalWrite(MuteLED_Pin, HIGH);
    }
    muteStateChange = false;
  }

  // Auto Unmute Logic
  // -------------------------------------------------
  if (autoUnmuteActive == true) {
    autoUnmuteTimeNow = millis();
    autoUnmuteTimeElapsed = autoUnmuteTimeNow - autoUnmuteTimeStart;
    if (autoUnmuteTimeElapsed >= autoUnmuteDuration) {
      isMuted = true;
      muteStateChange = true;
    }
  }

  // Sound Level Alerts
  // -------------------------------------------------
  // Sound Level 1 Alert
  if (micDeltaAverage > soundLowAlertLevel) {
    digitalWrite(SoundLevel1_Pin, HIGH);
  } else {
    digitalWrite(SoundLevel1_Pin, LOW);
  }

  // Sound Level 2 Alert
  if (micDeltaAverage > soundMidAlertLevel) {
    digitalWrite(SoundLevel2_Pin, HIGH);
  } else {
    digitalWrite(SoundLevel2_Pin, LOW);
  }

  // Sound Level 3 Alert
  if (micDeltaAverage > soundHighAlertLevel) {
    digitalWrite(SoundLevel3_Pin, HIGH);
    soundAlarm = true;
  } else {
    digitalWrite(SoundLevel3_Pin, LOW);
    //soundAlarm is turned off in the timing function below
  }

  // Alarm Logic
  // -------------------------------------------------
  if (soundAlarm == true) {
    alarmTimeNow = millis();
    alarmTimeElapsed = alarmTimeNow - alarmTimeStart;
    if (alarmTimeElapsed >= alarmDelayDuration) {
      if (((alarmCycles % 2) == 0) && (isMuted == false)) {       // Alternate High-Low Sounds
        digitalWrite(BuzzerOutput_Pin, HIGH);
      } else {
        digitalWrite(BuzzerOutput_Pin, LOW);
      }
      alarmCycles++;                      // Increment counter for each cycle
      alarmTimeStart = millis();          // Reset timer
    }

    // Turn off alarm when we reach maxAlarmCycles
    if (alarmCycles >= maxAlarmCycles) {
      soundAlarm = false;                        // Stop loop entry
      digitalWrite(BuzzerOutput_Pin, LOW);       // Make sure buzzer is off
      alarmCycles = 0;                           // Reset Counter after maximum
    }
  }


  // Toggle Built-in Lights On/Off
  // --------------------------------------------------------------------------
  // Turn on/off built in NodeMCU LED depending on status (LED is inverted)
  if ((systemOkay == true) && (nightMode == false)) {
    digitalWrite(StatusLED_Pin, LOW);
  } else {
    digitalWrite(StatusLED_Pin, HIGH);
  };

  // Turn on/off built in ESP8266 LED when Disconnected from Transmitter (LED is inverted)
  if ((syncOkay == true) && (nightMode == false)) {
    digitalWrite(SyncLED_Pin, LOW);
  } else {
    digitalWrite(SyncLED_Pin, HIGH);
  }


  //Check for WebSocket Stuff
  // -------------------------------------------------
  webSocket.loop();
}
