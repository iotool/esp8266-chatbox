# ESP8266 - Chatbox - Captive Portal

Chatbox enables local communication via Wifi between smartphones and tablets.

- wifi hotspot & captive portal redirect
- for local communication & information
- via smartphone, tablet and notebooks
- during power outage or blackout
- adhoc mesh network & emergency infrastructure

Multiple ESP devices automatically connect to each other to form a mesh network.

![ESP8266 Chatbox Chat](https://raw.githubusercontent.com/iotool/esp8266-chatbox/main/esp8266-chatbox-chat.jpg)

## How to use

Setup the Chatbox:
- place the Chatbox with visual contact to your neighbors
- a good location is the balcony, window sill, a flagpole or on the roof
- power the Chatbox / ESP8266-chip with a powerbank or by car battery
- with a 20.000 mAh powerbank the chatbox works 10 days
- place multiple Chatboxes every 50m to build a mesh network

Setup the Infopage
- connect to your Chatbox-ABCD via Wifi
- your smartphone open a popup with the homepage
- click OK to navigate to the chat
- click info link to open your infopage
- click change link to edit your infopage
- note useful infos, e.g. water points, frequency for emergency calls, etc.
- use your personal password (cliPwd at firmware) for change

Using the Chat
- every neighbor can connect to the open hotspot
- his smartphone will automatically redirect to the home page
- every user can create a new chat message (optional: sender, receiver)
- the chat message will display at the chat page
- the chat page automaticly reloads after some seconds
- every client will disconnect, if the maximum number of 8 devices is reached
- other clients join to the hotspot to read the chat

## How to install

- install Arduino at notebook or ArduinoDroid at Android
- download the source code to your device
- change the cliPwd at the source code
- upload the firmware to ESP8266 device

![ESP8266 Chatbox](https://raw.githubusercontent.com/iotool/esp8266-chatbox/main/esp8266-chatbox.jpg)

## Recommended ESP8266 devices

- Wemos D1 mini
- NodeMUC

### Extend range by additional antenna

* +5dBm = +50% by 6cm wire (dipole antenna)
* +9dBm = +100% by 5× 3cm wire (monopole antenna)
