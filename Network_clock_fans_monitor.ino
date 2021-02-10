#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <DYWiFiConfig.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

//------------------修改此处""内的信息-------------------//
char m = 1;												//0为24小时，1为上午，2为下午；若需开启12小时制，则填1或2均可
static const char ntpServerName[] = "time.windows.com"; //NTP服务器，微软
const int timeZone = 8;									//时区，北京时间为+8
static int scrnoff = 23;								//屏幕晚上关闭的时间，24小时制，不想关填24
static int scrnon = 8;									//屏幕早上开启时间，24小时制，不想关填0
String biliuid = "163844189";							//bilibili UID
//------------------------------------------------------//

DYWiFiConfig wificonfig;
ESP8266WebServer webserver(80);
#define DEF_WIFI_SSID "D1"
#define DEF_WIWI_PASSWORD "01234567890"
#define AP_NAME "flyAkari" //dev
void wificb(int c)
{
	Serial.print("=-=-=-=-");
	Serial.println(c);
}
//const int rs = 19, en = 20, d4 = 16, d5 = 7, d6 = 6, d7 = 5;
//LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
LiquidCrystal_I2C lcd(0x27, 16, 2);

WiFiUDP Udp;
unsigned int localPort = 8888; // 用于侦听UDP数据包的本地端口

const unsigned long HTTP_TIMEOUT = 5000;
WiFiClient client;
HTTPClient http;
String response;
int follower = 0;
const int slaveSelect = 5;
const int scanLimit = 7;

time_t getNtpTime();
void sendNTPpacket(IPAddress &address);
void oledClockDisplay();
void sendCommand(int command, int value);
void initdisplay();

boolean isNTPConnected = false;

void setup()
{
	Serial.begin(115200);
	while (!Serial)
		continue;
	Serial.println("NTP Clock oled version v1.1");
	//Serial.println("Designed by flyAkari");
	//lcd.begin(16, 2);
	lcd.init();
	initdisplay();

	lcd.setCursor(0, 0);
	lcd.print("Connect to Wifi");
	lcd.setCursor(0, 1);
	lcd.print("flyAkari");
	Serial.println("Screen Ready");

	Serial.print("Connecting WiFi...");
	wificonfig.begin(&webserver, "/");
	DYWIFICONFIG_STRUCT defaultConfig = wificonfig.createConfig();
	strcpy(defaultConfig.SSID, DEF_WIFI_SSID);
	strcpy(defaultConfig.SSID_PASSWORD, DEF_WIWI_PASSWORD);
	strcpy(defaultConfig.HOSTNAME, AP_NAME);
	strcpy(defaultConfig.APNAME, AP_NAME);
	wificonfig.setDefaultConfig(defaultConfig);
	wificonfig.enableAP();
	while (WiFi.status() != WL_CONNECTED)
	{
		wificonfig.handle(); //若不需要Web后台，可以注释掉此行
							 //Serial.println("Waiting for Connection...");
	}
	Serial.println("");
	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());
	Serial.println("Starting UDP");
	Udp.begin(localPort);
	Serial.print("Local port: ");
	Serial.println(Udp.localPort());
	Serial.println("waiting for sync");
	setSyncProvider(getNtpTime);
	setSyncInterval(300); //每300秒同步一次时间
	isNTPConnected = true;
	lcd.clear();
}

time_t prevDisplay = 0; //当时钟已经显示

void loop()
{
	if (WiFi.status() == WL_CONNECTED)
	{
		if (getJson())
		{
			if (parseJson(response))
			{
				displayNumber(follower);
			}
		}
	}
	else
	{
		Serial.println("[WiFi] Waiting to reconnect...");
		errorCode(0x1);
	}
	//delay(1000);

	if (timeStatus() != timeNotSet)
	{
		if (now() != prevDisplay)
		{ //时间改变时更新显示
			prevDisplay = now();
			oledClockDisplay();
		}
	}
	wificonfig.handle(); //若不需要Web后台，可以注释掉此行
}

void errorCode(byte errorcode)
{
	lcd.setCursor(9, 1);
	lcd.print("--E");
	lcd.print(errorcode);
	lcd.print("---");
}

void initdisplay()
{
	//lcd.clear();
	lcd.backlight();
	lcd.setCursor(0, 0);
	lcd.print("----------------");
	lcd.setCursor(0, 1);
	lcd.print("----------------");
	delay(500);
	lcd.clear();
}

void lcd_NightMode()
{
	lcd.setBacklight(0);
}

bool energysaving(int hours)
{
	if (hours >= scrnoff || hours < scrnon)
	{
		return true;
	}
	return false;
}

void oledClockDisplay()
{
	int years, months, days, hours, minutes, seconds, weekdays;
	years = year();
	months = month();
	days = day();
	hours = hour();
	minutes = minute();
	seconds = second();
	weekdays = weekday();
	Serial.printf("%d/%d/%d %d:%d:%d Weekday:%d\n", years, months, days, hours, minutes, seconds, weekdays);
	Serial.println("");
	if (energysaving(hours))
	{
		lcd_NightMode();
		//Serial.println("Screen is in the energy saving mode. ");
		//return;
	}
	else
		lcd.setBacklight(1);

	/*if (isNTPConnected)
	{
		u8g2.print("当前时间 (UTC+8)");
	}
	else
	{
		u8g2.print("无网络!");
	}*/
	//如果上次对时失败，则会显示无网络

	String currentTime = "";
	if (m != 0)
	{
		if (hours < 12)
			m = 1;
		else
			m = 2;
	}
	if (hours < 10)
	{
		currentTime += 0;
	}
	currentTime += hours;
	currentTime += ":";
	if (minutes < 10)
	{
		currentTime += 0;
	}
	currentTime += minutes;
	currentTime += ":";
	if (seconds < 10)
	{
		currentTime += 0;
	}
	currentTime += seconds;
	/*if (m != 0)
	{
		if (m == 2)
		{
			currentTime += "P";
		}
		else
		{
			currentTime += "A";
		}
	}*/
	String currentDay = "";
	currentDay += years;
	currentDay += "/";
	if (months < 10)
	{
		currentDay += 0;
	}
	currentDay += months;
	currentDay += "/";
	if (days < 10)
	{
		currentDay += 0;
	}
	currentDay += days;

	lcd.setCursor(0, 1);
	lcd.print(currentTime);
	lcd.setCursor(0, 0);
	lcd.print(currentDay);

	lcd.setCursor(11, 0);
	if (weekdays == 1)
		lcd.print("SU");
	else if (weekdays == 2)
		lcd.print("MO");
	else if (weekdays == 3)
		lcd.print("TU");
	else if (weekdays == 4)
		lcd.print("WE");
	else if (weekdays == 5)
		lcd.print("TH");
	else if (weekdays == 6)
		lcd.print("FR");
	else if (weekdays == 7)
		lcd.print("SA");
}
/*
1970-01-01 FR AM
00:00:00 8848488
*/

bool getJson()
{
	bool r = false;
	http.setTimeout(HTTP_TIMEOUT);
	http.begin("http://api.bilibili.com/x/relation/stat?vmid=" + biliuid);
	int httpCode = http.GET();
	if (httpCode > 0)
	{
		if (httpCode == HTTP_CODE_OK)
		{
			response = http.getString();
			//Serial.println(response);
			r = true;
		}
	}
	else
	{
		Serial.printf("[HTTP] GET JSON failed, error: %s\n", http.errorToString(httpCode).c_str());
		errorCode(0x2);
		r = false;
	}
	http.end();
	return r;
}

bool parseJson(String json)
{
	const size_t capacity = JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + 70;
	DynamicJsonDocument doc(capacity);
	deserializeJson(doc, json);

	int code = doc["code"];
	const char *message = doc["message"];

	if (code != 0)
	{
		Serial.print("[API]Code:");
		Serial.print(code);
		Serial.print(" Message:");
		Serial.println(message);
		errorCode(0x3);
		return false;
	}

	JsonObject data = doc["data"];
	unsigned long data_mid = data["mid"];
	int data_follower = data["follower"];
	if (data_mid == 0)
	{
		Serial.println("[JSON] FORMAT ERROR");
		errorCode(0x4);
		return false;
	}
	Serial.print("UID: ");
	Serial.print(data_mid);
	Serial.print(" follower: ");
	Serial.println(data_follower);

	follower = data_follower;
	return true;
}

void displayNumber(int number) //display number in the middle
{
	if (number < 0 || number > 9999999)
		return;
	lcd.setCursor(9, 1);
	lcd.print(number);
}

/*-------- NTP 代码 ----------*/

const int NTP_PACKET_SIZE = 48;		// NTP时间在消息的前48个字节里
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
			isNTPConnected = true;
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
	isNTPConnected = false;
	return 0; //如果未得到时间则返回0
}

// 向给定地址的时间服务器发送NTP请求
void sendNTPpacket(IPAddress &address)
{
	memset(packetBuffer, 0, NTP_PACKET_SIZE);
	packetBuffer[0] = 0b11100011; // LI, Version, Mode
	packetBuffer[1] = 0;		  // Stratum, or type of clock
	packetBuffer[2] = 6;		  // Polling Interval
	packetBuffer[3] = 0xEC;		  // Peer Clock Precision
	// 8 bytes of zero for Root Delay & Root Dispersion
	packetBuffer[12] = 49;
	packetBuffer[13] = 0x4E;
	packetBuffer[14] = 49;
	packetBuffer[15] = 52;
	Udp.beginPacket(address, 123); //NTP需要使用的UDP端口号为123
	Udp.write(packetBuffer, NTP_PACKET_SIZE);
	Udp.endPacket();
}
