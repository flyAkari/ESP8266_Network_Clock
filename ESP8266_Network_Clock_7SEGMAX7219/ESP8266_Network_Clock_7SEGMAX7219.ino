/**********************************************************************
 * 项目：8位数码管显示的NTP时钟+倒数日
 * 硬件：适用于NodeMCU ESP8266 + MAX7219驱动的8位数码管
 * 作者：flyAkari 会飞的阿卡林 bilibili UID:751219
 * 日期：2021/02/28
 **********************************************************************/
//硬件连接说明：
//MAX7219 --- ESP8266
//  VCC   --- 3V(3.3V)
//  GND   --- G (GND)
//  DIN   --- D7(GPIO13)
//  CS    --- D1(GPIO5)
//  CLK   --- D5(GPIO14)
/**********************************************************************
 * 使用说明：
 * 初次上电后，用任意设备连接热点WiFi：flyAkari，等待登录页弹出或浏览器输入
 * 192.168.4.1进入WiFi及时钟配置页面，输入待连接WiFi名和密码，选择倒数日期
 * 或正数日期，填写时区(-12~12)，填全后提交。若连接成功，则开发板会记住以上
 * 配置的信息，并在下次上电时自动连接WiFi并显示时间，热点和配置页面不再出现。
 * 如需更改倒数日或WiFi信息，请关闭原WiFi阻止其自动连接，上电后10秒无法登录
 * 则会重新开启热点和配置页面。默认显示时间，短接D6和地线即显示倒数日。
***********************************************************************/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <EEPROM.h>
#include <TimeLib.h>

static const char ntpServerName[] = "ntp1.aliyun.com"; //NTP服务器地址
int timeZone = 8;                                      //时区
tmElements_t tgdate;                                   //倒数日目的日期
time_t tgdateUnixTime;                                 //倒数日目的日期时间戳

/*#################################### EEPROM #########################################*/
typedef struct
{                  //存储配置结构体
    int tz;        //时间戳
    time_t tgunix; //倒数日期时间戳
} config_type;
config_type config;

void saveConfig()
{ //存储配置到"EEPROM"
    Serial.println("save config");
    EEPROM.begin(sizeof(config));
    uint8_t *p = (uint8_t *)(&config);
    for (uint i = 0; i < sizeof(config); i++)
    {
        EEPROM.write(i, *(p + i));
    }
    EEPROM.commit(); //此操作会消耗flash写入次数
}

void loadConfig()
{ //从"EEPROM"加载配置
    Serial.println("load config");
    EEPROM.begin(sizeof(config));
    uint8_t *p = (uint8_t *)(&config);
    for (uint i = 0; i < sizeof(config); i++)
    {
        *(p + i) = EEPROM.read(i);
    }
    timeZone = config.tz;
    tgdateUnixTime = config.tgunix;
}

/*#################################### Web配置 #########################################*/
char sta_ssid[32] = {0};          //暂存WiFi名
char sta_password[64] = {0};      //暂存WiFi密码
const char *AP_NAME = "flyAkari"; //自定义8266AP热点名
//配网及目标日期设定html页面
const char *page_html = "\
<!DOCTYPE html>\r\n\
<html lang='en'>\r\n\
<head>\r\n\
  <meta charset='UTF-8'>\r\n\
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>\r\n\
  <title>Document</title>\r\n\
</head>\r\n\
<body>\r\n\
  <h1>ESP8266配置页</h1>\r\n\
  <form name='input' action='/' method='POST'>\r\n\
    WiFi名称:\r\n\
    <input type='text' name='ssid'><br>\r\n\
    WiFi密码:\r\n\
    <input type='password' name='password'><br>\r\n\
    目标日期:\r\n\
    <input type='date' name='date'><br>\r\n\
    时区(-12~12, 默认为8——北京时间):<br>\r\n\
    <input type='text' name='timezone' value='8'><br>\r\n\
    <input type='submit' value='提交'>\r\n\
    <br><br>\r\n\
    <a href='https://space.bilibili.com/751219'>FlyAkari</a>\r\n\
  </form>\r\n\
</body>\r\n\
</html>\r\n\
";
const byte DNS_PORT = 53;       //DNS端口号默认为53
IPAddress apIP(192, 168, 4, 1); //8266 APIP
DNSServer dnsServer;
ESP8266WebServer server(80);

void connectWiFi();

void handleRoot()
{
    server.send(200, "text/html", page_html);
}
void handleRootPost()
{
    Serial.println("handleRootPost");
    if (server.hasArg("ssid"))
    {
        Serial.print("ssid:");
        strcpy(sta_ssid, server.arg("ssid").c_str());
        Serial.println(sta_ssid);
    }
    else
    {
        Serial.println("[WebServer]Error, SSID not found!");
        server.send(200, "text/html", "<meta charset='UTF-8'>Error, SSID not found!"); //返回错误页面
        return;
    }
    if (server.hasArg("password"))
    {
        Serial.print("password:");
        strcpy(sta_password, server.arg("password").c_str());
        Serial.println(sta_password);
    }
    else
    {
        Serial.println("[WebServer]Error, PASSWORD not found!");
        server.send(200, "text/html", "<meta charset='UTF-8'>Error, PASSWORD not found!");
        return;
    }
    if (server.hasArg("date"))
    {
        Serial.print("target date:");
        char tempdate[32] = {0};
        strcpy(tempdate, server.arg("date").c_str());
        Serial.println(tempdate);
        char *pToken = NULL;
        char *pSave = NULL;
        pToken = strtok_r(tempdate, "-", &pSave); //时间字符串分割
        tgdate.Year = (atoi(pToken)) - 1970;
        pToken = strtok_r(NULL, "-", &pSave);
        tgdate.Month = atoi(pToken);
        pToken = strtok_r(NULL, "-", &pSave);
        tgdate.Day = atoi(pToken);
        tgdate.Hour = 0;
        tgdate.Minute = 0;
        tgdate.Second = 0;
        tgdateUnixTime = makeTime(tgdate);
        Serial.print("target date unixtime:");
        Serial.println(tgdateUnixTime);
        config.tgunix = tgdateUnixTime;
    }
    else
    {
        Serial.println("[WebServer]Error, TARGET DATE not found!");
        server.send(200, "text/html", "<meta charset='UTF-8'>Error, TARGET DATE not found!");
        return;
    }
    if (server.hasArg("timezone"))
    {
        Serial.print("timezone:");
        char timeZone_s[4];
        strcpy(timeZone_s, server.arg("timezone").c_str());
        timeZone = atoi(timeZone_s);
        if(timeZone>13||timeZone<-13)
        {
            timeZone = 8;
        }
        Serial.println(timeZone);
        config.tz = timeZone;
    }
    else
    {
        Serial.println("[WebServer]Error, TIMEZONE not found!");
        server.send(200, "text/html", "<meta charset='UTF-8'>Error, TIMEZONE not found!");
        return;
    }
    if (server.hasArg("clock"))
    {
        Serial.print("isClock:");
        Serial.println(server.arg("clock"));
    }
    server.send(200, "text/html", "<meta charset='UTF-8'>提交成功"); //返回保存成功页面
    delay(2000);
    //一切设定完成，连接wifi
    saveConfig();
    connectWiFi();
}

void connectWiFi()
{
    WiFi.mode(WIFI_STA);       //切换为STA模式
    WiFi.setAutoConnect(true); //设置自动连接
    WiFi.begin(sta_ssid, sta_password);
    Serial.println("");
    Serial.print("Connect WiFi");
    int count = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        count++;
        if (count > 20)
        { //10秒过去依然没有自动连上，开启Web配网功能，可视情况调整等待时长
            Serial.println("Timeout! AutoConnect failed");
            WiFi.mode(WIFI_AP); //开热点
            WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
            if (WiFi.softAP(AP_NAME))
            {
                Serial.println("ESP8266 SoftAP is on");
            }
            server.on("/", HTTP_GET, handleRoot);      //设置主页回调函数
            server.onNotFound(handleRoot);             //设置无法响应的http请求的回调函数
            server.on("/", HTTP_POST, handleRootPost); //设置Post请求回调函数
            server.begin();                            //启动WebServer
            Serial.println("WebServer started!");
            if (dnsServer.start(DNS_PORT, "*", apIP))
            { //判断将所有地址映射到esp8266的ip上是否成功
                Serial.println("start dnsserver success.");
            }
            else
                Serial.println("start dnsserver failed.");
            Serial.println("Please reset your WiFi setting.");
            Serial.println("Connect the WiFi named flyAkari, the configuration page will pop up automatically, if not, use your browser to access 192.168.4.1");
            break; //启动WebServer后便跳出while循环，回到loop
        }
        Serial.print(".");
        if (WiFi.status() == WL_CONNECT_FAILED)
        {
            Serial.print("password:");
            Serial.print(WiFi.psk().c_str());
            Serial.println(" is incorrect");
        }
        if (WiFi.status() == WL_NO_SSID_AVAIL)
        {
            Serial.print("configured SSID:");
            Serial.print(WiFi.SSID().c_str());
            Serial.println(" cannot be reached");
        }
    }
    Serial.println("");
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("WiFi Connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        server.stop();
        dnsServer.stop();
        //WiFi连接成功后，热点便不再开启，无法再次通过web配网
        //若WiFi连接断开，ESP8266会自动尝试重新连接，直至连接成功，无需代码干预
        //如需要更换WiFi，请在关闭原WiFi后重启ESP8266，否则上电后会自动连接原WiFi，也就无法进入配网页面
    }
}

/*#################################### NTP时钟 #########################################*/
WiFiUDP Udp;
unsigned int localPort = 8888; // 用于侦听UDP数据包的本地端口
time_t getNtpTime();
void sendNTPpacket(IPAddress &address);

const int NTP_PACKET_SIZE = 48;     // NTP时间在消息的前48个字节里
byte packetBuffer[NTP_PACKET_SIZE]; // 输入输出包的缓冲区

time_t getNtpTime()
{
    IPAddress ntpServerIP; // NTP服务器的地址

    while (Udp.parsePacket() > 0)
        ; // 丢弃以前接收的任何数据包
    Serial.println("Transmit NTP Request");
    // 从池中获取随机服务器
    WiFi.hostByName(ntpServerName, ntpServerIP);
    Serial.print(ntpServerName);
    Serial.print(": ");
    Serial.println(ntpServerIP);
    sendNTPpacket(ntpServerIP);
    uint32_t beginWait = millis();
    while (millis() - beginWait < 1500)
    {
        int size = Udp.parsePacket();
        if (size >= NTP_PACKET_SIZE)
        {
            Serial.println("Receive NTP Response");
            Udp.read(packetBuffer, NTP_PACKET_SIZE); // 将数据包读取到缓冲区
            unsigned long secsSince1900;
            // 将从位置40开始的四个字节转换为长整型，只取前32位整数部分
            secsSince1900 = (unsigned long)packetBuffer[40] << 24;
            secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
            secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
            secsSince1900 |= (unsigned long)packetBuffer[43];
            Serial.println(secsSince1900);
            Serial.println(secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR);
            return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
        }
    }
    Serial.println("No NTP Response :-("); //无NTP响应
    return 0;                              //如果未得到时间则返回0
}

// 向给定地址的时间服务器发送NTP请求
void sendNTPpacket(IPAddress &address)
{
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    packetBuffer[0] = 0b11100011; // LI, Version, Mode
    packetBuffer[1] = 0;          // Stratum, or type of clock
    packetBuffer[2] = 6;          // Polling Interval
    packetBuffer[3] = 0xEC;       // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;
    Udp.beginPacket(address, 123); //NTP需要使用的UDP端口号为123
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    Udp.endPacket();
}

/*#################################### 倒数日 #########################################*/
int daysBetweenTwoTimestamp(time_t srcStamp, time_t dstStamp)
{ //倒数日返回正数，正数日返回负数，同一天为0
    if (dstStamp >= srcStamp)
    {
        return (dstStamp + 86399 - srcStamp) / 86400;
    }
    else
    {
        return (srcStamp - dstStamp) / 86400 * (-1);
    }
}

/*#################################### 数码管显示 #########################################*/
const int slaveSelect = 5;
const int scanLimit = 7;

void sendCommand(int command, int value)
{
    digitalWrite(slaveSelect, LOW);
    SPI.transfer(command);
    SPI.transfer(value);
    digitalWrite(slaveSelect, HIGH);
}

void initdisplay()
{ //显示"--------
    for (int i = 8; i > 0; i--)
    {
        sendCommand(i, 0xa);
    }
}

void digitalClockDisplay()
{ //显示hh-mm-ss
    int years, months, days, hours, minutes, seconds, weekdays;
    years = year();
    months = month();
    days = day();
    hours = hour();
    minutes = minute();
    seconds = second();
    weekdays = weekday();
    Serial.printf("%d/%d/%d %d:%d:%d Weekday:%d\n", years, months, days, hours, minutes, seconds, weekdays);
    sendCommand(8, hours / 10);
    sendCommand(7, hours % 10);
    sendCommand(6, 0xa);
    sendCommand(5, minutes / 10);
    sendCommand(4, minutes % 10);
    sendCommand(3, 0xa);
    sendCommand(2, seconds / 10);
    sendCommand(1, seconds % 10);
}

void displayNumber(int number)
{                         //居中显示数字
    number = abs(number); //负数不显示负号
    if (number < 0 || number > 99999999)
        return;
    int x = 1;
    int tmp = number;
    for (x = 1; tmp /= 10; x++)
        ;
    for (int i = 1; i < 9; i++)
    {
        if (i < (10 - x) / 2 || i >= (x / 2 + 5))
        {
            sendCommand(i, 0xf);
        }
        else
        {
            int character = number % 10;
            sendCommand(i, character);
            number /= 10;
        }
    }
}

void digitalDaysMatterDisplay()
{ //显示倒数日
    int years, months, days, hours, minutes, seconds, weekdays;
    years = year();
    months = month();
    days = day();
    hours = hour();
    minutes = minute();
    seconds = second();
    weekdays = weekday();
    Serial.printf("%d/%d/%d %d:%d:%d Weekday:%d\n", years, months, days, hours, minutes, seconds, weekdays);
    Serial.print(now());
    Serial.print(" ");
    Serial.println(tgdateUnixTime);
    Serial.print("Days Left(+) or have passed(-): ");
    Serial.println(daysBetweenTwoTimestamp(now(), tgdateUnixTime));
    displayNumber(daysBetweenTwoTimestamp(now(), tgdateUnixTime));
}

/*#################################### Arduino #########################################*/
void setup()
{
    Serial.begin(115200);
    while (!Serial)
        continue;
    Serial.println("ESP8266 NTPClock and DaysMatter, version v1.0 by flyAkari");
    SPI.begin();
    pinMode(slaveSelect, OUTPUT);
    digitalWrite(slaveSelect, LOW);
    sendCommand(12, 1);         //Shutdown,open
    sendCommand(15, 0);         //DisplayTest,no
    sendCommand(10, 3);         //Intensity,15(max)
    sendCommand(11, scanLimit); //ScanLimit,8-1=7
    sendCommand(9, 255);        //DecodeMode,Code B decode for digits 7-0
    digitalWrite(slaveSelect, HIGH);
    initdisplay();
    Serial.println("LED Ready");
    loadConfig();
    Serial.print("Connecting WiFi...");
    WiFi.hostname("Smart-ESP8266");
    connectWiFi();
    Serial.println("Starting UDP");
    Udp.begin(localPort);
    Serial.print("Local port: ");
    Serial.println(Udp.localPort());
    Serial.println("waiting for sync");
    setSyncProvider(getNtpTime);
    setSyncInterval(300);      //每300秒同步一次时间
    pinMode(D6, INPUT_PULLUP); //内部上拉
}

time_t prevDisplay = 0; // 当时钟已经显示
void loop()
{
    server.handleClient();
    dnsServer.processNextRequest();
    if (timeStatus() != timeNotSet)
    {
        if (now() != prevDisplay)
        { //时间改变时更新显示
            prevDisplay = now();
            if (digitalRead(D6) == 1) //D6未接地显示时间
                digitalClockDisplay();
            else                      //否则显示倒数日
                digitalDaysMatterDisplay();
        }
    }
}

/*
鸣谢以下文章作者：
https://blog.csdn.net/weixin_44220583/article/details/111562423
*/
