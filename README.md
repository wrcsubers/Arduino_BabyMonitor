# NodeMCU Baby Monitor
### A sound activated baby monitor that uses NodeMCUs and Websockets for real time communication between transmitter and receiver(s). 

![Image of BabyMonitor Group](https://github.com/wrcsubers/Arduino_BabyMonitor/blob/main/_Images/Arduino_BabyMonitor_TransmitterReceiver.png)

### Status: Functional

### YouTube Video: (link)

## Transmitter
#### Hardware

![Image of BabyMonitor Transmitter](https://github.com/wrcsubers/Arduino_BabyMonitor/blob/main/_Images/Arduino_BabyMonitor_Transmitter.png)

The transmitter hardware consists of a NodeMCU with a microphone and amplifier circuit.  The output of this circuit is fed into the ADC of the NodeMCU.  Sound level is determined by sampling the input repeatedly, then calculating the difference between the largest and smallest values.  This is called the Mic Delta value.  During low levels of noise, the Mic Delta value is small, which then increases with the loudness of the sound being measured.  I found this to be the most reliable/accurate way of determining sound levels near the microphone.  

* 1/2 Size Breadboard with:
  * Amica NodeMCU v1.0
  * Electret Microphone
  * LM358N Op-Amp
  * Various Capacitors, Resistors and Jumper Wires

* The two on-board LEDs on the transmitter show the following:
  * NodeMCU LED (closest one to USB port) - Shows system status, solid when Websocket server is up and running.
  * ESP8266 LED (on WiFi chip) - On when the low sound level threshold is exceeded.
* I used the microphone from a KY-037 module, however, I don't recommend these modules as a whole.  They have very poor sensitivity and are extremely difficult to fine tune to different sound levels.
* I'm not super familiar with using Op-Amps and Microphones, so I followed this very helpful guide: https://lowvoltage.wordpress.com/2011/05/21/lm358-mic-amp/ 

#### Software
The transmitter software consists mainly of a Websocket and HTTP Server with additional processing logic for sound levels.  

The sound processing logic works as follows:
  1.  Sound level is measured every 50ms.  This value is compared to previous min/max values and updated if needed.
  2.  After 4 measurements (~200ms), the difference between the largest and smallest of the 4 measurements is added to a circular buffer.
  3.  Each time a new value is added to the circular buffer, the average of multiple values is calculated.  The number of values that are averaged together is based upon the 'micDeltaSamplesToAvg' which is a value between 2-50.  This value can be modified on the webapp by adjusting the 'Mic Input Smoothing' slider.
  4.  Once the average is calculated, the value is broadcast to all connected clients (~200ms/update).

Any other values changed via the webapp are broadcast to the server, which then re-broadcasts the updated values to all connected clients.

The transmitter also runs a Multicast-DNS Service to facilitate easy communication between transmitter/receiver.  For systems that don't support mDNS, you can browse the web page by visiting the IP Address of the transmitter.  Upon startup, the transmitter dynamically creates a Javascript file containing the current IP Address of the transmitter.  This is then read by the web page to connect to the Websocket port.  This method allows the use of both mDNS and IP Address resolution/browsing of the web interface.



## Receiver
#### Hardware

![Image of BabyMonitor Receiver](https://github.com/wrcsubers/Arduino_BabyMonitor/blob/main/_Images/Arduino_BabyMonitor_Receiver.png)

The receiver consists of a NodeMCU with visual and audible notification elements.  Additionally, the receiver has a button which allows the user to mute the alarm.

* 1/2 Size Breadboard with:
  * Amica NodeMCU v1.0
  * Green, Yellow, Red, and Blue LEDs
  * Omron Button Switch
  * Active Buzzer
  * Various Resistors and Jumper Wires

* The two on-board LEDs on the receiver show the following:
  * NodeMCU LED (closest one to USB port) - Shows system status, solid on when receiver is up connected to WiFi.
  * ESP8266 LED (on WiFi chip) - Solid on when connected to the transmitter's Websocket server.

The LEDs and Buzzer could be replaced to facilitate different notifications as well.  Originally, I used some WS2812 LEDs for visual notification, but ended up opting for a simple 3 LED system of Green (Low Alert), Yellow (Mid Alert), and Red (High Alert).  The Blue LED turns on when the Buzzer is muted and off when it is not.  A simple Active Buzzer, which cycles on and off rapidly, creates the alarm sound.  This could also be replaced with a Passive Buzzer and some PWM output from the NodeMCU.

#### Software
The receiver software consists mainly of a WebSocket client and additional processing logic for the audio and visual elements.

Upon connection to the transmitter, the receiver gets the current values for Alert Levels and then continuous values of the Mic Delta Average.  The receiver compares the value of the Mic Delta Average to the Alert Levels and then changes LEDs accordingly.  If the High Alert value is exceeded, the receiver will sound the buzzer for a set period of time.  If temporary suspension of the buzzer is needed, a press of the mute button silences the buzzer.  This muting will stay on until the button is pressed again, or it will be automatically un-muted after 5 minutes.



## Web Interface

![Image of BabyMonitor Web Interface Main](https://github.com/wrcsubers/Arduino_BabyMonitor/blob/main/_Images/Arduino_BabyMonitor_WebApp.png)

The web interface of the Baby Monitor allows users to change settings on the fly.  This is useful to fine tune the output in various situations without having to connect the transmitter/receiver to a computer.  The web interface also shows the current value of the Mic Delta Average output and can even be used as a visual receiver (One could easily add a sound alert to the page if wanted).

Expand the settings section on the web interface to reveal the following:
* Night Mode
  * Off (default) - Built-in Blue LEDs on transmitter and receiver show their states as explained above.
  * On - Built-in Blue LEDs on transmitter and receiver are turned off and do not show any activity.  Night mode does not affect the Alert Level/Mute LEDs on the receiver.

* Alert Values
  * Low - If the 'Current Mic Delta Value' exceeds this value, the green light on the receiver turns on.
  * Mid - Same as above, but the yellow light turns on.
  * High - Same as above, but the red light turns on AND the audible alarm sounds.

* Mic Input Smoothing - The amount of smoothing to apply to the Mic Delta Value.  A higher value here means that the sound level needs to be high for a longer period of time to increase the Current Mic Delta Value.  At it's lowest setting, you will basically get instant feedback of sound level changes, although the values are very erratic.



## Setup/Installation
1. Build the transmitter/receiver boards as shown in images above (hi-res images in _Images/HiRes folder).
2. Install Arduino IDE (version used: 1.8.13) from here: https://www.arduino.cc/en/software
3. Setup the NodeMCU for the Arduino environment using this guide: https://github.com/esp8266/Arduino
4. Setup LittleFS, be sure to follow the bottom part of this section specifically for LittleFS: https://arduino-esp8266.readthedocs.io/en/latest/filesystem.html#uploading-files-to-file-system
5. Clone/Download the following folders/files: https://github.com/wrcsubers/Arduino_BabyMonitor/tree/main/_Code
6. Modify the .ino files to include your WiFi information
7. Upload the BabyMonitor_Receiver.ino file to the receiver using the Arduino IDE.
8. Upload the BabyMonitor_Transmitter.ino file to the transmitter using the Arduino IDE.
9. Upload the data folder to the Transmitter from the Arduino IDE using: Arduino IDE > Tools > ESP8266 LittleFS Data Upload
10. The transmitter/receiver should automatically connect to each other, reference the status light explanations above.
11. You can find the IP Address of the transmitter during startup by watching the serial monitor.
12. Visit the IP Address (or default mDNS name, 'babymonitor.local') of the transmitter from your computer or mobile devices browser.
13. Any changes made on the web interface are updated instantly across all devices.

Enjoy!

### Notes
* Web interface tested as working on iOS 14.4 and Firefox 86.0
* I don't have any Android devices to test the web interface on, but it should work without issue.

* Special thanks to everyone below for their excellent work:
  * https://github.com/Links2004/arduinoWebSockets
  * https://github.com/rlogiacco/CircularBuffer
  * https://github.com/esp8266/Arduino
