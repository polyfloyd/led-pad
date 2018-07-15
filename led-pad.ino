#include <limits.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// WiFi settings
const char ssid[] = "";
const char pass[] = "";
const char* mqtt_server = "";

// MQTT stuff
char ID[9] = {0};
WiFiClient espClient;
PubSubClient mqttClient(espClient);

const char* mqttTopic = "bitlair/leds/lounge";
const char* mqttDebugTopic = "bitlair/debug";
const uint8_t inputPin = D2; // active high
const unsigned int waitDuration = 6000; // Milliseconds to wait after press


const int JOY_BTN = D1;
// The ESP8266 has only one analog pin. So to measure both the horizontal and
// vertical outputs from the joystick, we use a 4051 analog multiplexer.
const int AMUX_S0 = D0;

const int OLED_RES  = D2;
const int OLED_DC   = D3;
const int OLED_CS   = D4;
const int OLED_SCLK = D5;
const int OLED_MOSI = D6;

Adafruit_ST7735 display = Adafruit_ST7735(OLED_CS, OLED_DC, OLED_MOSI, OLED_SCLK, OLED_RES);

void setup() {
    uint32_t chipid = ESP.getChipId();
    snprintf(ID, sizeof(ID), "%x", chipid);

    Serial.begin(115200);
    Serial.println();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    pinMode(A0, INPUT);
    pinMode(AMUX_S0, OUTPUT);
    pinMode(JOY_BTN, INPUT);

    display.initR(INITR_MINI160x80);


    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "display size: %dx%d", display.width(), display.height());
    Serial.println(buf);

    // XXX
//    ESP.wdtDisable();

    // We start by connecting to a WiFi network
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        Serial.print(".");
    }
    Serial.println();

    Serial.print("WiFi connected to: ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    Serial.println();

    mqttClient.setServer(mqtt_server, 1883);
    mqttClient.setCallback(mqttCallback);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    /*Serial.print("Message arrived [");
      Serial.print(topic);
      Serial.print("] ");
      for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
      }
      Serial.println();*/
}

void reconnectMQTT() {
    // Loop until we're reconnected
    for (int connectCount = 1; !mqttClient.connected(); connectCount++) {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (mqttClient.connect(ID)) {
            Serial.println("connected");
            // Once connected, publish an announcement...
            char buf[64] = {0};
            snprintf(buf, sizeof(buf), "%s (re)connect #%ld", "projectName", connectCount);
            Serial.println(buf);
            mqttClient.publish(mqttDebugTopic, buf);
            // ... and resubscribe
//            mqttClient.subscribe(mqttTopic);
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void mqttPublish(const char *topic, const char *message) {
    Serial.print(topic);
    Serial.print(" -> ");
    Serial.print(message);
    if (!mqttClient.publish(topic, message, false)) {
        Serial.println(" FAIL");
    } else {
        Serial.println(" OK");
    }
}

void readJoystickPosition(float *x, float *y) {
    digitalWrite(AMUX_S0, HIGH);
    int rx = analogRead(A0);
    digitalWrite(AMUX_S0, LOW);
    int ry = analogRead(A0);
    *x = float(rx - 512) / 512.0;
    *y = float(ry - 512) / 512.0;
}


uint16_t color(uint8_t r, uint8_t g, uint8_t b) {
    // red = 5 bits at 0xF800
    // green = 6 bits green at 0x07E0
    // blue = 5 bits at 0x001F
    return uint16_t(r >> 3) << 11 | uint16_t(g >> 2) << 5 | uint16_t(b >> 3);
}

void hsv2rgb(float h, float s, float v, float *r, float *g, float *b) {
    if (s <= 0.0) {
        *r = v;
        *g = v;
        *b = v;
        return;
    }
    float hh = h;
    if (hh >= 360.0) hh = 0.0;
    hh /= 60.0;
    int i = (int)hh;
    float ff = hh - i;
    float p = v * (1.0 - s);
    float q = v * (1.0 - (s * ff));
    float t = v * (1.0 - (s * (1.0 - ff)));

    switch(i) {
    case 0:
        *r = v;
        *g = t;
        *b = p;
        break;
    case 1:
        *r = q;
        *g = v;
        *b = p;
        break;
    case 2:
        *r = p;
        *g = v;
        *b = t;
        break;

    case 3:
        *r = p;
        *g = q;
        *b = v;
        break;
    case 4:
        *r = t;
        *g = p;
        *b = v;
        break;
    case 5:
    default:
        *r = v;
        *g = p;
        *b = q;
        break;
    }
}

void renderUI() {
    display.fillScreen(0x0000);
//    for (int x = display.width() / 2; x < display.width() - 4; x++) {
//        for (int y = display.height() / 2; y < display.height() - 4; y++) {
    for (int x = 0; x < display.width() - 0; x++) {
        for (int y = 0; y < display.height() - 0; y++) {
            float h = float(y) / float(display.height()) * 360;
            float s = 1;
            float v = float(x) / float(display.width());
            float rf, gf, bf;
            hsv2rgb(h, s, v, &rf, &gf, &bf);
            uint8_t r = rf * 255;
            uint8_t g = gf * 255;
            uint8_t b = bf * 255;
            display.drawPixel(x, y, color(r, g, b));
        }
        yield();
    }
}

void loop() {
    if (!mqttClient.connected()) {
        reconnectMQTT();
    }

    float x, y;
    readJoystickPosition(&x, &y);
    Serial.print(x);
    Serial.print(", ");
    Serial.println(y);

    renderUI();

    mqttClient.loop();
    yield();
}
