# async_webserver_test
 Simple test creating a webserver using ESPAsyncWebServer (https://github.com/me-no-dev/ESPAsyncWebServer) to control a MAX7219 led matrix with eight 8x8 panels.
 
 Start with WiFi in access point mode with captive portal if no WiFi connection can be made. Once a connection has been made disable the DNS server and start mDNS, and if disconnected just keep trying to reconnect.
```
esp32   level convertor   5v   max7219
--------------------------|-----------
 3v3----lv           hv---+----vcc
 gnd----gnd         gnd--------gnd
mosi----lv1         hv1--------din
  cs----lv2         hv2--------cs
 clk----lv3         hv3--------clk

esp32:
mosi    = 23
cs      = 05
sck/clk = 18
```
