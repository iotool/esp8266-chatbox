
# Firmware ESP8266 Chatbox

The source code of this the firmware is one file so that it can be copied as easily as possible. It was avoided to use additional libraries, so that the source code can be compiled with a standard installation of Arduino. The code is divided into sections for each component.

In addition to the source code you can download the ready compiled firmware for Wemos D1 mini and upload it to the ESP8266.

## Problem statement

The starting point for the development of this solution was how to communicate locally with direct neighbors in the event of a power outage or blackout and cellular network failure.

Very few neighbors are likely to have walkie-talkies and radio units. Simple devices are often not compatible with each other in terms of frequencies and opening tones, i.e. they are unsuitable for local communication with many neighbors.

Everyone has a smartphone these days. However, if power and mobile communications fail, the Internet as a basis for communication is lost.

Although there are various apps where smartphones can communicate directly with each other via Bluetooth, they also lack a standardized basis for spontaneous communication with one's neighbors.

## Solution approach

WiFi access points, which are connected to each other via a mesh network and powered by battery or powerbank, are suitable as a basis for communication.

Instead of a previously installed app, a web-based interface is used. The web interface can be used independently of the operating system for Android, iOS and Windows. The application must be intuitive and simple to allow as many neighbors as possible to use it.

## Challenges with ESP8266

As a technical base I decided to use the ESP8266, because this WiFi microcontroller is very cheap and common. RasperryPi would also be conceivable, but it consumes six times more power and is more expensive.

The ESP chip has limited RAM and a moderate CPU performance. For this reason, the SDK limits to a maximum of 8 stations per access point. If there is not enough free RAM, the ESP hangs up. The integrated flash memory only allows storing a maximum of up to 2 MB of files.

The standard mesh library overlays the normal WiFi communication and does not allow simultaneous use as an access point. In addition, new nodes in the mesh network are not supplied with the previous chat history. Other mesh networks limit the number of hops between nodes or require a master-slave topology.

## Dealing with the limitations

With my first prototypes I had massive problems with memory consumption. Each request to the access point consumed RAM memory that was not released. As a workaround, the free RAM is monitored and the ESP is reset if necessary. Via the serial interface as well as CLI status page the RAM consumption is displayed.

Infrequent accesses allow the used RAM to be released again. Many smartphones apps regularly access to the Internet in the background. The access point redirects all requests to the home page. That's why the home page is static and so small that it is transmitted within one WiFi frame.

Also, mDNS resolutions for the web.local domain unnecessarily generates additional requests. That is why the main path is redirected to the IP address. Also the chat application runs on the IP address instead of the domain name.

To limit the number of requests, the automatic reload of the chat page waits 8 seconds, i.e. on average only one access per second for 8 stations.

There is also a 4 second delay when creating new chat messages. This limits the throughput of messages in the mesh network and enables sharing from the mesh stack.

## Stack mesh network 

Other mesh networks route messages specifically to a defined recipient or log the path for the response. Messages are sent repeatedly several times and the number of hops is limited.

My approach works completely differently. The basis for all nodes is a common first-in-first-out stack. The messages in the stack are sorted by their age. The age of each message increases each node independently. Old messages are deleted when the stack is full. 

The mesh network is implemented in such a way that the access point remains usable for smartphones. The ESP acts as an access point and station. The common prefix of the access point name is used to identify other mesh nodes in the environment. 

Each mesh node connects as a station at the access point at other mesh nodes and downloads their mesh stack and sorts the messages into its own mesh stack.

This method has high latency, but it works very robustly and the range or hops is limited by updating with new messages.

## Automatic disconnect

Unfortunately, the ESP SDK does not include a way to disconnect inactive stations from the access point. 

As soon as 8 stations are connected to the access point, a timeout starts which disconnects all stations from the access point and restarts it.

If the 8th station was another mesh node, then it disconnects again after the stack download and the timeout is stopped.

Once disconnected, all waiting smartphones and mesh nodes can reconnect to the access point. This workaround allows more than 8 devices roundrobin to connect to the ESP. Until the next timeout, the smartphones can retrieve and display the chat history. Additional mesh nodes increase the number of smartphones that can communicate via the network.
