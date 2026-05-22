#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include "Audio.h"

// ==================== 1. 配置参数区 ====================
#define DEVICE_NAME     "ESP32-S3 Music Player"
#define DEFAULT_SSID    "YOUR_WIFI_SSID"        // 默认WiFi名称
#define DEFAULT_PASSWORD "YOUR_WIFI_PASSWORD"    // 默认WiFi密码
#define DEVICE_UUID     "2f402f80-da50-11e1-9b23-001788092242"

// 屏幕引脚定义 (ST7789 1.54" TFT)
#define TFT_SCL 12
#define TFT_SDA 11
#define TFT_RES 10
#define TFT_DC  9
#define TFT_BLK 21
#define TFT_CS  1

// 音频功放引脚定义 (MAX98357A)
#define I2S_BCLK 15
#define I2S_LRC  16
#define I2S_DIN  7

// 常用颜色定义 (RGB565)
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_BLUE    0x001F
#define COLOR_GREEN   0x07E0
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F

// ==================== 2. 全局实例对象 ====================
Preferences preferences;
AsyncWebServer server(80);
Audio audio;

// 屏幕驱动初始化
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCL, TFT_SDA, -1);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RES, 0 /* 旋转 */, true /* IPS */, 240 /* 宽 */, 240 /* 高 */);

// 播放状态全局变量
String currentTitle = "Ready to stream";
String currentArtist = "DLNA Player";
String currentStatus = "STOPPED";
int currentVolume = 70; // 默认手机端音量初始设为 70% 避免声音过小
bool isApMode = false;
bool volumeChanged = false; // 用于通知主循环同步音量硬件

// ==================== 3. 屏幕刷新逻辑 ====================
void updateDisplay(bool forceRedraw = false) {
  static String lastTitle = "";
  static String lastArtist = "";
  static String lastStatus = "";
  static int lastVolume = -1;

  if (forceRedraw) {
    gfx->fillScreen(COLOR_BLACK);
    gfx->setTextColor(COLOR_WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(10, 10);
    gfx->print("ESP32-S3 Player");
    gfx->drawFastHLine(0, 40, 240, COLOR_BLUE);
  }

  if (forceRedraw) {
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_WHITE);
    gfx->setCursor(10, 215);
    if (isApMode) {
      gfx->print("AP Mode: ESP32-S3-Music-AP");
    } else {
      gfx->print("IP: " + WiFi.localIP().toString());
    }
  }

  if (lastStatus != currentStatus || forceRedraw) {
    gfx->fillRect(10, 50, 220, 20, COLOR_BLACK);
    gfx->setTextColor(COLOR_GREEN);
    gfx->setTextSize(2);
    gfx->setCursor(10, 50);
    gfx->print("Status: " + currentStatus);
    lastStatus = currentStatus;
  }

  if (lastVolume != currentVolume || forceRedraw) {
    gfx->fillRect(10, 80, 220, 20, COLOR_BLACK);
    gfx->setTextColor(COLOR_YELLOW);
    gfx->setTextSize(2);
    gfx->setCursor(10, 80);
    gfx->print("Volume: " + String(currentVolume) + "%");
    lastVolume = currentVolume;
  }

  if (lastTitle != currentTitle || forceRedraw) {
    gfx->fillRect(10, 120, 220, 40, COLOR_BLACK);
    gfx->setTextColor(COLOR_CYAN);
    gfx->setTextSize(2);
    gfx->setCursor(10, 120);
    gfx->print(currentTitle.substring(0, 32)); 
    lastTitle = currentTitle;
  }

  if (lastArtist != currentArtist || forceRedraw) {
    gfx->fillRect(10, 170, 220, 30, COLOR_BLACK);
    gfx->setTextColor(COLOR_MAGENTA);
    gfx->setTextSize(1.5);
    gfx->setCursor(10, 170);
    gfx->print("Artist: " + currentArtist.substring(0, 40));
    lastArtist = currentArtist;
  }
}

// ==================== 4. SSDP 发现与主动广播任务 ====================
void ssdpTask(void *pvParameters) {
  WiFiUDP udp;
  udp.beginMulticast(IPAddress(239, 255, 255, 250), 1900);
  char packetBuffer[512];
  unsigned long lastNotifyTime = 0;

  while (1) {
    unsigned long now = millis();
    
    if (now - lastNotifyTime > 15000 || lastNotifyTime == 0) {
      lastNotifyTime = now;
      IPAddress ip = WiFi.localIP();
      
      String notify1 = "NOTIFY * HTTP/1.1\r\n"
                       "HOST: 239.255.255.250:1900\r\n"
                       "CACHE-CONTROL: max-age=1800\r\n"
                       "LOCATION: http://" + ip.toString() + "/dlna/description.xml\r\n"
                       "SERVER: DLNADOC/1.50 UPnP/1.0 " + String(DEVICE_NAME) + "/1.0\r\n"
                       "NT: upnp:rootdevice\r\n"
                       "NTS: sdp:alive\r\n"
                       "USN: uuid:" + String(DEVICE_UUID) + "::upnp:rootdevice\r\n\r\n";
                       
      String notify2 = "NOTIFY * HTTP/1.1\r\n"
                       "HOST: 239.255.255.250:1900\r\n"
                       "CACHE-CONTROL: max-age=1800\r\n"
                       "LOCATION: http://" + ip.toString() + "/dlna/description.xml\r\n"
                       "SERVER: DLNADOC/1.50 UPnP/1.0 " + String(DEVICE_NAME) + "/1.0\r\n"
                       "NT: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
                       "NTS: sdp:alive\r\n"
                       "USN: uuid:" + String(DEVICE_UUID) + "::urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n";

      udp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
      udp.write((const uint8_t*)notify1.c_str(), notify1.length());
      udp.endPacket();
      
      udp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
      udp.write((const uint8_t*)notify2.c_str(), notify2.length());
      udp.endPacket();
    }

    int packetSize = udp.parsePacket();
    if (packetSize) {
      int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
      if (len > 0) {
        packetBuffer[len] = '\0';
        String request = String(packetBuffer);
        
        if (request.indexOf("M-SEARCH") >= 0) {
          IPAddress remoteIP = udp.remoteIP();
          uint16_t remotePort = udp.remotePort();
          
          WiFiUDP replyUdp;
          replyUdp.beginPacket(remoteIP, remotePort);
          String reply = "HTTP/1.1 200 OK\r\n"
                         "CACHE-CONTROL: max-age=1800\r\n"
                         "EXT:\r\n"
                         "LOCATION: http://" + WiFi.localIP().toString() + "/dlna/description.xml\r\n"
                         "SERVER: DLNADOC/1.50 UPnP/1.0 " + String(DEVICE_NAME) + "/1.0\r\n"
                         "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
                         "USN: uuid:" + String(DEVICE_UUID) + "::urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n";
          replyUdp.write((const uint8_t*)reply.c_str(), reply.length());
          replyUdp.endPacket();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ==================== 5. Web 服务器与 DLNA 协议控制解析 ====================
void initWebServer() {
  // 1. 基础配网页面 (AP模式访问)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<html><head><meta charset='UTF-8'><title>WiFi 配网</title></head>"
                  "<body><h2>" + String(DEVICE_NAME) + " 配网后台</h2>"
                  "<form method='POST' action='/save'>"
                  "SSID: <input type='text' name='ssid'><br><br>"
                  "密码: <input type='password' name='password'><br><br>"
                  "<input type='submit' value='保存并重启设备'>"
                  "</form></body></html>";
    request->send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
      String newSsid = request->getParam("ssid", true)->value();
      String newPass = request->getParam("password", true)->value();
      
      preferences.begin("wifi-config", false);
      preferences.putString("ssid", newSsid);
      preferences.putString("password", newPass);
      preferences.end();
      
      request->send(200, "text/plain", "配置已保存，设备正在重启中...");
      delay(2000);
      ESP.restart();
    } else {
      request->send(400, "text/plain", "参数缺失错误");
    }
  });

  // 2. DLNA 设备能力描述 XML
  server.on("/dlna/description.xml", HTTP_GET, [](AsyncWebServerRequest *request){
    String xml = "<?xml version=\"1.0\"?>\r\n"
                 "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\r\n"
                 "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
                 "  <device>\r\n"
                 "    <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>\r\n"
                 "    <friendlyName>" + String(DEVICE_NAME) + "</friendlyName>\r\n"
                 "    <manufacturer>Espressif</manufacturer>\r\n"
                 "    <modelName>ESP32-S3 DLNA Player</modelName>\r\n"
                 "    <UDN>uuid:" + String(DEVICE_UUID) + "</UDN>\r\n"
                 "    <serviceList>\r\n"
                 "      <service>\r\n"
                 "        <serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>\r\n"
                 "        <serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>\r\n"
                 "        <SCPDURL>/dlna/AVTransport.xml</SCPDURL>\r\n"
                 "        <controlURL>/dlna/AVTransport</controlURL>\r\n"
                 "        <eventSubURL>/dlna/AVTransportEvent</eventSubURL>\r\n"
                 "      </service>\r\n"
                 "      <service>\r\n"
                 "        <serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>\r\n"
                 "        <serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>\r\n"
                 "        <SCPDURL>/dlna/RenderingControl.xml</SCPDURL>\r\n"
                 "        <controlURL>/dlna/RenderingControl</controlURL>\r\n"
                 "        <eventSubURL>/dlna/RenderingControlEvent</eventSubURL>\r\n"
                 "      </service>\r\n"
                 "    </serviceList>\r\n"
                 "  </device>\r\n"
                 "</root>\r\n";
    request->send(200, "text/xml", xml);
  });

  server.on("/dlna/AVTransport.xml", HTTP_GET, [](AsyncWebServerRequest *request){
    String xml = "<?xml version=\"1.0\"?>\r\n"
                 "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
                 "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
                 "  <actionList>\r\n"
                 "    <action><name>SetAVTransportURI</name></action>\r\n"
                 "    <action><name>Play</name></action>\r\n"
                 "    <action><name>Pause</name></action>\r\n"
                 "    <action><name>Stop</name></action>\r\n"
                 "  </actionList>\r\n"
                 "</scpd>\r\n";
    request->send(200, "text/xml", xml);
  });

  server.on("/dlna/RenderingControl.xml", HTTP_GET, [](AsyncWebServerRequest *request){
    String xml = "<?xml version=\"1.0\"?>\r\n"
                 "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
                 "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
                 "  <actionList>\r\n"
                 "    <action><name>SetVolume</name></action>\r\n"
                 "    <action><name>GetVolume</name></action>\r\n"
                 "  </actionList>\r\n"
                 "</scpd>\r\n";
    request->send(200, "text/xml", xml);
  });

  // 3. AVTransport 播放动作控制实体路由接口
  server.on("/dlna/AVTransport", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/xml", "");
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    String body = "";
    for (size_t i = 0; i < len; i++) body += (char)data[i];
    String responseXml = "";

    if (body.indexOf("SetAVTransportURI") >= 0) {
      int startIdx = body.indexOf("<CurrentURI>");
      int endIdx = body.indexOf("</CurrentURI>");
      if (startIdx >= 0 && endIdx >= 0) {
        startIdx += 12;
        String url = body.substring(startIdx, endIdx);
        url.trim();
        url.replace("&amp;", "&");
        
        currentTitle = "Loading Dynamic Stream...";
        currentArtist = "Network Stream";
        currentStatus = "PLAYING";
        
        audio.connecttohost(url.c_str());
      }
      responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:SetAVTransportURIResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body></s:Envelope>";
    }
    else if (body.indexOf("Play") >= 0) {
      if(!audio.isRunning()) audio.pauseResume();
      currentStatus = "PLAYING";
      responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><u:PlayResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body></s:Envelope>";
    }
    else if (body.indexOf("Pause") >= 0) {
      if(audio.isRunning()) audio.pauseResume();
      currentStatus = "PAUSED";
      responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:PauseResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body></s:Envelope>";
    }
    else if (body.indexOf("Stop") >= 0) {
      audio.stopSong();
      currentStatus = "STOPPED";
      currentTitle = "Ready to stream";
      currentArtist = "DLNA Player";
      responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:StopResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body></s:Envelope>";
    }

    request->send(200, "text/xml", responseXml);
  });

  // 4. RenderingControl 音量控制实体路由接口
  server.on("/dlna/RenderingControl", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/xml", "");
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    String body = "";
    for (size_t i = 0; i < len; i++) body += (char)data[i];
    String responseXml = "";

    if (body.indexOf("SetVolume") >= 0) {
      int startIdx = body.indexOf("<DesiredVolume>");
      int endIdx = body.indexOf("</DesiredVolume>");
      if (startIdx >= 0 && endIdx >= 0) {
        startIdx += 15;
        currentVolume = body.substring(startIdx, endIdx).toInt();
        if (currentVolume < 0) currentVolume = 0;
        if (currentVolume > 100) currentVolume = 100;

        // 【优化修复一】：修正映射规则。ESP32-audioI2S 库中 setVolume 传入参数越大声音越大 (0-21级)
        // 手机端发送 0-100% -> 正向等比映射到底层的 0-21 级音量范围
        int hwVol = (currentVolume * 21) / 100;
        audio.setVolume(hwVol);
        volumeChanged = true; // 产生同步标记
      }
      responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:SetVolumeResponse xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\"/></s:Body></s:Envelope>";
    }
    else if (body.indexOf("GetVolume") >= 0) {
      responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                    "<s:Body><u:GetVolumeResponse xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
                    "<CurrentVolume>" + String(currentVolume) + "</CurrentVolume>"
                    "</u:GetVolumeResponse></s:Body></s:Envelope>";
    }

    request->send(200, "text/xml", responseXml);
  });

  server.begin();
}

// ==================== 6. Arduino 生命周期 ====================
void setup() {
  Serial.begin(115200);

  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);
  gfx->begin();
  
  updateDisplay(true);

  // 初始化功放 I2S 硬件引脚映射
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DIN);
  
  // 【优化修复二】：开机初始化大音量（正向映射：15级属于偏大音量，最大21）
  audio.setVolume(15); 

  preferences.begin("wifi-config", true);
  String ssid = preferences.getString("ssid", DEFAULT_SSID);
  String password = preferences.getString("password", DEFAULT_PASSWORD);
  preferences.end();

  WiFi.begin(ssid.c_str(), password.c_str());
  int retryCounter = 0;
  while (WiFi.status() != WL_CONNECTED && retryCounter < 20) {
    delay(500);
    Serial.print(".");
    retryCounter++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    isApMode = false;
    
    xTaskCreatePinnedToCore(ssdpTask, "ssdp_task", 4096, NULL, 3, NULL, 0);
  } else {
    Serial.println("\nWiFi Connection Failed. Starting AP Mode...");
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-S3-Music-AP", "12345678");
    isApMode = true;
  }

  updateDisplay(true);
  initWebServer();
}

void loop() {
  // 核心解码高频轮询
  audio.loop();

  // 【优化修复三】：如果在网络中断时或部分App通过侧边按键调节音量，高频同步应用底层设置
  if (volumeChanged) {
    volumeChanged = false;
    int hwVol = (currentVolume * 21) / 100;
    audio.setVolume(hwVol);
  }

  static unsigned long lastUiTick = 0;
  if (millis() - lastUiTick > 500) {
    lastUiTick = millis();
    updateDisplay(false);
  }
}

// ==================== 7. 流媒体音频流回调 ====================
void audio_showstreamtitle(const char *info) {
  if (info && strlen(info) > 0) {
    currentTitle = String(info);
    Serial.printf("Metadata Title Changed: %s\n", info);
  }
}

void audio_id3data(const char *info) {
  String id3Str = String(info);
  if (id3Str.startsWith("Artist:")) {
     currentArtist = id3Str.substring(7);
     currentArtist.trim();
  }
}

void audio_eof_stream(const char *info) {
  currentStatus = "STOPPED";
  currentTitle = "Ready to stream";
  currentArtist = "DLNA Player";
}