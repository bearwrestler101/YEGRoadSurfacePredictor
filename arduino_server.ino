#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>

#include <Time.h>
#include <stdio.h>
#include <time.h>

byte led_brightness[3];
std::vector<long unsigned> alarms;

unsigned long ntp_time, internal_time;

unsigned long getCurrentTime()
{
  return (time(NULL)-internal_time) + ntp_time;
}

class NTPConnection {
    const static int NTP_PACKET_SIZE = 48;

    WiFiUDP udp_;
    unsigned int local_port_ = 6969;
    byte packet_buffer_[NTP_PACKET_SIZE];

    IPAddress server_ip_;
    const char *server_name_ = "time.nist.gov";

  public:

    void initConnection();
    void randomServer();
    void sendPacket();
    unsigned long parsePacket();

} ntp_connection;

void NTPConnection::initConnection() {
  udp_.begin(local_port_);
}

void NTPConnection::randomServer() {
  WiFi.hostByName(server_name_, server_ip_);
}

void NTPConnection::sendPacket() {
  memset(packet_buffer_, 0, NTP_PACKET_SIZE);

  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packet_buffer_[0] = 0b11100011;   // LI, Version, Mode
  packet_buffer_[1] = 0;     // Stratum, or type of clock
  packet_buffer_[2] = 6;     // Polling Interval
  packet_buffer_[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packet_buffer_[12]  = 49;
  packet_buffer_[13]  = 0x4E;
  packet_buffer_[14]  = 49;
  packet_buffer_[15]  = 52;

  udp_.beginPacket(server_ip_, 123);
  udp_.write(packet_buffer_, NTP_PACKET_SIZE);
  udp_.endPacket();
}

unsigned long NTPConnection::parsePacket() {
  int cb = udp_.parsePacket();
  if (!cb) {
    Serial.println("No NTP packet recieved.");
    return 0UL;
  }
  else {
    udp_.read(packet_buffer_, NTP_PACKET_SIZE);

    unsigned high_word = word(packet_buffer_[40], packet_buffer_[41]);
    unsigned low_word = word(packet_buffer_[42], packet_buffer_[43]);

    unsigned long secs_since_1900 = high_word << 16 | low_word;
    const unsigned long seventy_years = 2208988800UL;
    unsigned long epoch = secs_since_1900 - seventy_years;

    ntp_time = epoch;
    internal_time = time(NULL);
    return epoch;
  }
}

class ServerConnection {
    ESP8266WebServer server;

    void handleRoot_();
    void handleLED_();
    void handleAlarm_();
    void initRoutes_();

  public:
    void initServer();
    void tick();

} server;

const char root[] = "<html>\
  <body>\
    <h1>POST plain text to /setled/</h1><br>\
    <form method=\"post\" enctype=\"text/x-www-form-urlencoded\" action=\"/setled/\">\
      <label>Red</label>\
      <input type=\"number\"><br>\
      <label>Blue</label>\
      <input type=\"number\"><br>\
      <label>Green</label>\
      <input type=\"number\"><br>\
      <input type=\"submit\" value=\"Submit\">\
    </form>\
    <h1>POST form data to /setalarm/</h1><br>\
    <form method=\"post\" enctype=\"text/plain\" action=\"/setalarm/\">\
        <label>Wake up time</label>\
      <input type=\"datetime-local\"><br>\
      <input type=\"submit\" value=\"Submit\">\
    </form>\
  </body>\
</html>";

void ServerConnection::handleRoot_() {

  server.send(200, "text/html", root);
}

void ServerConnection::handleLED_() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
  }
  else {
    server.send(200, "text/plain", server.arg("plain"));
    for (int i = 0; i < server.args(); ++i) {
      led_brightness[i] = server.arg(i).toInt();
    }
  }
}

void ServerConnection::handleAlarm_() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
  }
  else {
    server.send(200, "text/plain", server.arg("plain"));
    tm t;
    strptime(server.arg("plain").c_str(), /*TODO:*/"", &t);
    long unsigned epoch = static_cast<long unsigned>(mktime(&t));
    alarms.push_back(epoch);
  }
}

void ServerConnection::initRoutes_() {
  server.on("/", [ = ]() {
    this->handleRoot_();
  });
  server.on("/setled/", [ = ]() {
    this->handleLED_();
  });
  server.on("/setalarm/", [ = ]() {
    this->handleAlarm_();
  });
}

void ServerConnection::initServer() {
  server.begin();
  initRoutes_();
  Serial.println("HTTP server started.");
}

void ServerConnection::tick() {
  server.handleClient();
}

#ifndef SSID
#define SSID "ssid"
#define SSID_PSK "password"
#endif

const char *ssid = SSID;
const char *pass = SSID_PSK;

void connectWifi() {
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.print("Connected as ");
  Serial.println(WiFi.localIP());
}
#define RED_LED 4
#define BLUE_LED 5
#define GREEN_LED 6
void setup()
{
  Serial.begin(115200);
  connectWifi();
  ntp_connection.initConnection();
  server.initServer();
  for (int i = 0; i < 3; ++i)
    led_brightness[i] = 0;
    
  pinMode(RED_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  
}

void checkAlarms() {
  for (int i = 0; i < alarms.size(); ++i) {
    if (alarms[i] - getCurrentTime() < 15 * 60 * 1000) {
      for (int j = 0; j < 3; ++j) {
        led_brightness[j] = byte(max(0.0, 15.0 - (alarms[i] - getCurrentTime()) / (1000.0 * 60.0) ) * 255);
      }
    }
  }
  std::remove_if(alarms.begin(), alarms.end(), [](unsigned long t){return t < getCurrentTime();});
}

void loop()
{
  if (time(NULL)-internal_time > 20000){
    ntp_connection.randomServer();
    ntp_connection.sendPacket();
    delay(500);
    ntp_connection.parsePacket();
  }
  server.tick();

  checkAlarms();

  analogWrite(RED_LED, led_brightness[0]);
  analogWrite(BLUE_LED, led_brightness[1]);
  analogWrite(GREEN_LED, led_brightness[2]);
}
