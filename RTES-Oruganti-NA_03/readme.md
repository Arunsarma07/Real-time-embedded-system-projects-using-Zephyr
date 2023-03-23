IoT devices are often small, remotely deployed and infrequently serviced. These devices are also network-constrained and limited in both computing resources and power consumption. As a consequence, to establish communication with IoT devices, we should only consider the protocols that address IoT data connectivity as well as the constrained environment.

In this assignment, I have implemented several components/applications to demonstrate the operations and communication of an IoT device in Zephyr RTOS (version 2.6.0) running on MIMXRT1050-EVKB board.

task includes:

Port an existing HC-SR04 driver to Zephyr version 2.6 for MIMXRT1050_EVK. The driver can be found in https://github.com/inductivekickback/hc-sr04. Modifications should be done such that it can report the distance measure in inches (with 2 decimal digits) and consist of a Kalman filter to reduce any measurement noise of HC-SR04 sensors.

Develop a Zephyr application that serves as a COAP server using Zephyr’s network stack and COAP library. The server should enable the following resources:

/sensor/hcsr_i where i=0 and 1 for two HC-SR04 sensors. A get method request retrieves the current distance measure. The sensors are COAP observing resources and a notification should be sent to all observers whenever there is a change of distance measurement larger than 0.5 inches.
/sensor/period. The interface is used to define the sampling period of HC-SR04 sensor measurement which is set by a put request method.
/led/led_n where n= r, g, and b for a RGD led. A put method request sets the led to 0 (OFF) or 1 (ON) and a get method request retrieves the current led status.
/.well-known/core interface for resource discovery.
Your MIMXRT1050_EVK board can be connected to an Ethernet-based LAN or directly connected to a computer with an Ethernet cable. If there is a DHCP server on the LAN, you can enable dhcp service to acquire a dynamic ipv4 address, as shown in the Zephyr’s sample application /samples/net/dhcpv4_client. Otherwise, a static address can be set for the board. For peripheral connections, please use the following Arduino pins:

Trigger and echo pins of hcsr_0: D3 and D6.
Trigger and echo pins of hcsr_1: D8 and D9.
Led RGB pins: D2, D5, and D12.
In the current Zepyr source tree, there is a COAP server sample application /samples/net/sockets/coap_server/src/coap-server.c. You may want to study its implementation carefully or re-use the code partially in your development. To test your COAP server, we will use a Chrome extension, Copper for Chrome (Cu4Cr) CoAP user-agent, to browse and interact with Internet of Things devices via COAP protocol. The user-agent and the installation procedure can be found in https://github.com/mkovatsc/Copper4Cr.
