
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SPIFlash.h>
#include <Adafruit_ThinkInk.h>
#include <SdFat.h>
#include <WiFi.h>
// #include <Fonts/FreeSans9pt7b.h>
// #include <SPI.h>
#include <Adafruit_GFX.h>
// #include <Adafruit_ImageReader_EPD.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

// credentials and other sensitive information
#include "secrets.h"

#define METRIC false
#define C_TO_F(c) (c * 1.8 + 32.0)
#define KPH_TO_MPH(kph) (kph * 0.621371)


// Build the URL path as a C-style string
const char *URL = "api.open-meteo.com";
const char *PATH = "/v1/forecast?latitude=%s&longitude=%s&current=%s&daily=%s&timeformat=%s&timezone=%s";
const char *CURRENT = "temperature_2m,wind_speed_10m";
const char *DAILY = "weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,"
					"wind_speed_10m_max,wind_direction_10m_dominant";
const char *TIME_FORMAT = "unixtime";


// String representation of days and months
const char *DAYS[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
const char *MONTHS[] = {
	"January",
	"February",
	"March",
	"April",
	"May",
	"June",
	"July",
	"August",
	"September",
	"October",
	"November",
	"December",
};

// Weather Code Information from https://open-meteo.com/en/docs
// Code 	Description
// 0 	Clear sky
// 1, 2, 3 	Mainly clear, partly cloudy, and overcast
// 45, 48 	Fog and depositing rime fog
// 51, 53, 55 	Drizzle: Light, moderate, and dense intensity
// 56, 57 	Freezing Drizzle: Light and dense intensity
// 61, 63, 65 	Rain: Slight, moderate and heavy intensity
// 66, 67 	Freezing Rain: Light and heavy intensity
// 71, 73, 75 	Snow fall: Slight, moderate, and heavy intensity
// 77 	Snow grains
// 80, 81, 82 	Rain showers: Slight, moderate, and violent
// 85, 86 	Snow showers slight and heavy
// 95 * 	Thunderstorm: Slight or moderate
// 96, 99 * 	Thunderstorm with slight and heavy hail

const int WMO_CODE_TO_ICON[] = {
	0,										// 0 = sunny
	1,										// 1 = partly sunny/cloudy
	2,										// 2 = cloudy
	3,										// 3 = very cloudy
	61, 63, 65,								// 4 = rain
	51, 53, 55, 80, 81, 82,					// 5 = showers
	95, 96, 99,								// 6 = storms
	56, 57, 66, 67, 71, 73, 75, 77, 85, 86, // 7 = snow
	45, 48,									// 8 = fog and stuff
};

// Create SPIFlash object
Adafruit_FlashTransport_ESP32 flashTransport;
Adafruit_SPIFlash flash(&flashTransport);
FatVolume filesys;
File32 file;
// Adafruit_ImageReader_EPD reader(filesys);

WiFiClientSecure client;
ThinkInk_290_Grayscale4_T5 display(EPD_DC, EPD_RESET, EPD_CS, -1, EPD_BUSY);
Adafruit_NeoPixel intneo = Adafruit_NeoPixel(4, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

class RawMonochromeBitmap
{
public:
	RawMonochromeBitmap() : data(nullptr), width(0), height(0) {}

	~RawMonochromeBitmap()
	{
		if (data)
		{
			delete[] data;
		}
	}

	bool loadFromBMP(const char *filename)
	{
		if (!file.open(filename, O_RDONLY))
		{
			Serial.println(F("Failed to open file."));
			return false;
		}
		uint32_t fileSize = file.size();

		// Read the BMP header (54 bytes)
		uint8_t bmpHeader[54];
		if (file.read(bmpHeader, 54) != 54)
		{
			Serial.println(F("Unexpected end of file"));
			return false;
		}

		// Get image dimensions from the BMP header
		width = bmpHeader[18] | (bmpHeader[19] << 8) | (bmpHeader[20] << 16) |
				(bmpHeader[21] << 24);
		height = bmpHeader[22] | (bmpHeader[23] << 8) | (bmpHeader[24] << 16) |
				 (bmpHeader[25] << 24);

		if ((bmpHeader[28] | (bmpHeader[29] << 8)) != 1)
		{
			Serial.println(bmpHeader[28] | (bmpHeader[29] << 8));
			Serial.println(F("Not a monochrome bitmap"));
			return false;
		}

		// Get the offset to the bitmap data
		uint32_t offsetBytes = bmpHeader[10] | (bmpHeader[11] << 8) |
							   (bmpHeader[12] << 16) | (bmpHeader[13] << 24);

		int32_t bmpRowSize = ((((width) + 7) / 8) + 3) & ~3;
		int32_t rawRowSize = ((width) + 7) / 8;

		if (fileSize != offsetBytes + (bmpRowSize * height))
		{
			Serial.println(fileSize);
			Serial.println(offsetBytes + (bmpRowSize * height));
			Serial.println(F("File size does not match expected size"));
			return false;
		}

		// Allocate a buffer for the padded bitmap data
		uint8_t *bmpData = new uint8_t[bmpRowSize * height];

		// Seek to the bitmap data offset
		file.seekSet(offsetBytes);
		// Read the bitmap data into the buffer
		file.read(bmpData, bmpRowSize * height);
		file.close();

		// Allocate a buffer for the raw bitmap data
		data = new uint8_t[rawRowSize * height];

		// Copy the bitmap data row by row, excluding the padding, and flip vertically
		for (int y = 0; y < height; y++)
		{
			uint8_t *rowStart = bmpData + (height - 1 - y) * bmpRowSize;
			memcpy(data + y * rawRowSize, rowStart, rawRowSize);
		}

		// // Invert all the bits in the data buffer
		// for (int i = 0; i < rawRowSize * height; i++)
		// {
		// 	data[i] = ~data[i];
		// }

		delete[] bmpData;
		return true;
	}

	void draw(int16_t x, int16_t y)
	{
		if (data)
		{
			display.clearBuffer();
			display.drawBitmap(x, y, data, width, height, EPD_WHITE, EPD_BLACK);
			display.display();
		}
	}

private:
	uint8_t *data;
	int32_t width;
	int32_t height;
};

void deepSleep(uint64_t sleepSeconds = 3600)
{
	// give EPD and Serial time to catch up
	delay(1000);

	pinMode(NEOPIXEL_POWER, OUTPUT);
	pinMode(SPEAKER_SHUTDOWN, OUTPUT);
	digitalWrite(SPEAKER_SHUTDOWN, LOW); // off
	digitalWrite(NEOPIXEL_POWER, HIGH);	 // off
	digitalWrite(EPD_RESET, LOW);		 // off (yes required to save a few mA)
	pinMode(13, OUTPUT);
	digitalWrite(13, LOW);

	// close client
	client.stop();

	// close filesystem and flash
	filesys.end();
	flash.end();

	esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000); // 60 seconds
	esp_deep_sleep_start();
}

void mountFileSystem()
{

	// Initialize flash library and check its chip ID.
	if (!flash.begin())
	{
		Serial.println(F("Error, failed to initialize flash chip!"));
		while (1)
		{
		}
	}
	// Serial.print(F("Flash chip JEDEC ID: 0x"));
	// Serial.println(flash.getJEDECID(), HEX);

	// First call begin to mount the filesystem.  Check that it returns true
	// to make sure the filesystem was mounted.
	if (!filesys.begin(&flash))
	{
		Serial.println(F("Failed to mount filesystem!"));
		Serial.println(
			F("Was CircuitPython loaded on the board first to create the "
			  "filesystem?"));
		while (1)
		{
		}
	}
	// Serial.println(F("Mounted filesystem!"));
}

void setupWifi()
{

	// Serial.print(F("Attempting to connect to SSID: "));
	// Serial.println(SSID);

	display.clearBuffer();
	// display.setFont(&FreeSans9pt7b);
	display.setTextSize(0);
	display.setTextColor(EPD_BLACK);
	display.setCursor(10, 30);

	display.print(F("Connecting to SSID "));
	display.println(SSID);

	display.display();

	WiFi.begin(SSID, PASSWORD);

	// attempt to connect to Wifi network:
	while (WiFi.status() != WL_CONNECTED && WiFi.status() != WL_CONNECT_FAILED)
	{
		// Serial.print(".");
		delay(100);
	}

	if (WiFi.status() == WL_CONNECT_FAILED)
	{
		Serial.print(F("Failed to connect to SSID"));
		deepSleep(60);
	}

	// Serial.print(F("Connected to "));
	// Serial.println(SSID);
}

void drawBackground()
{
	RawMonochromeBitmap bg;
	// ImageReturnCode result;
	// result = reader.drawBMP((char *)"bmps/weather_bg.bmp", display, 0, 0, false);
	if (!bg.loadFromBMP("weather_bg.bmp"))
	{
		Serial.println(F("Unable to load background"));
		while (1)
		{
		}
	}
	bg.draw(0, 0);
	// Serial.println("completed draw");
}

JsonDocument getWeatherForecast()
{

	// Serial.println(F("Starting connection to server..."));
	// client.setCACert(test_root_ca);
	// client.setCertificate(test_client_key); // for client verification
	// client.setPrivateKey(test_client_cert);	// for client verification
	client.setInsecure();

	if (!client.connect(URL, 443))
	{
		Serial.println(F("Connection failed!"));
		deepSleep(60);
	}
	// Serial.println(F("Connected to server!"));

	// built the HTTP request path
	char pathbuf[300] = {0};
	snprintf(pathbuf, sizeof(pathbuf), PATH, LATITUDE, LONGITUDE, CURRENT, DAILY, TIME_FORMAT, TIME_ZONE);
	// Serial.println(pathbuf);

	// Make a HTTP request:
	client.print(F("GET "));
	client.print(pathbuf);
	client.println(F(" HTTP/1.1"));
	client.print(F("Host: "));
	client.println(URL);
	client.println(F("Connection: close"));
	client.println();

	// Check HTTP status
	char status[64] = {0};
	client.readBytesUntil('\r', status, sizeof(status));
	// Serial.println(status);
	if (strcmp(status, "HTTP/1.1 200 OK") != 0)
	{
		Serial.print(F("Unexpected response: "));
		Serial.println(status);
		deepSleep(60);
	}

	while (client.connected())
	{
		String line = client.readStringUntil('\n');
		if (line == "\r")
		{
			// Serial.println(F(" received"));
			break;
		}
	}

	while (client.peek() != '{' && client.peek() != EOF)
	{
		client.read();
	}

	if (client.peek() == EOF)
	{
		Serial.print(F("Unexpected response"));
		deepSleep(60);
	}

	JsonDocument doc;

	DeserializationError error = deserializeJson(doc, client);
	if (error)
	{
		Serial.print(F("deserializeJson() failed: "));
		Serial.println(error.c_str());
		deepSleep(60);
	}

	return doc;
}

void setup()
{
	// Initialize serial
	Serial.begin(115200);
	// Wait for port to open
	// while (!Serial);

	// setup pins
	pinMode(BUTTON_A, INPUT_PULLUP);
	pinMode(BUTTON_B, INPUT_PULLUP);
	pinMode(BUTTON_C, INPUT_PULLUP);
	pinMode(BUTTON_D, INPUT_PULLUP);
	pinMode(EPD_BUSY, INPUT);
	pinMode(LED_BUILTIN, OUTPUT);

	// turn on built in LED
	digitalWrite(LED_BUILTIN, HIGH); // on

	// setup display
	display.begin(THINKINK_GRAYSCALE4);

	// mount the embedded flash file system to load bitmaps
	mountFileSystem();

	// connect to wifi network
	setupWifi();

	// Check if a "bmps/weather_bg.bmp exists and draw it
	drawBackground();

	// get the weather forecast
	JsonDocument doc = getWeatherForecast();

	serializeJsonPretty(doc, Serial);

	// Extract values
	// JsonObject root_0 = doc[0];
	// Serial.println(F("Response:"));
	// const char* root_0_text = root_0["text"];
	// const char* root_0_author = root_0["author"];

	// Serial.print("Quote: "); Serial.println(root_0_text);
	// Serial.print("Author: "); Serial.println(root_0_author);

	// display.clearBuffer();
	// display.setFont(&FreeSans9pt7b);
	// display.setTextSize(1);
	// display.setTextWrap(true);
	// display.setTextColor(EPD_BLACK);
	// display.setCursor(10, 30);
	// display.println(root_0_text);
	// display.setTextColor(EPD_DARK);
	// display.setCursor(40, 120);
	// display.println(root_0_author);
	// display.display();
	// while (!digitalRead(EPD_BUSY)) {
	//   delay(10);
	// }

	// while (client.available() > 0)
	// {
	//   //read back one line from the server
	//   String line = client.readStringUntil('\r');
	//   Serial.println(line);
	// }

	// disconnect
	// client.stop();

	// ensure EPD has time to refresh
	deepSleep();
}

void loop() {}
