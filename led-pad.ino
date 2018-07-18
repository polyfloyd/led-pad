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

const char* MQTT_TOPIC = "bitlair/leds/lounge";
const char* MQTT_TOPIC_DEBUG = "bitlair/debug";
const uint8_t inputPin = D2; // active high
const unsigned int waitDuration = 6000; // Milliseconds to wait after press


const int JOY_BTN = D1;
// The ESP8266 has only one analog pin. So to measure both the horizontal and
// vertical outputs from the joystick, we use a 4051 analog multiplexer.
const int AMUX_S0 = D0;

const int OLED_RES = D2;
const int OLED_DC  = D3;
const int OLED_CS  = D4;

Adafruit_ST7735 display = Adafruit_ST7735(OLED_CS, OLED_DC, OLED_RES);

void setup() {
    uint32_t chipid = ESP.getChipId();
    snprintf(ID, sizeof(ID), "%x", chipid);

    Serial.begin(115200);
    Serial.println();

    // Perform dummy read to perform calibration.
    float _x, _y;
    readJoystickPosition(&_x, &_y);

    pinMode(A0, INPUT);
    pinMode(AMUX_S0, OUTPUT);
    pinMode(JOY_BTN, INPUT);

    display.initR(INITR_MINI160x80);
    display.invertDisplay(true);
    display.fillScreen(ST77XX_BLACK);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Connect to the wireless network
    Serial.print("Connecting to: ");
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

void mqttCallback(char* topic, byte* payload, unsigned int len) {
    payload[len] = 0;
    Serial.print(topic);
    Serial.print(" <- ");
    Serial.println((char*)payload);
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
            mqttClient.publish(MQTT_TOPIC_DEBUG, buf);
            // ... and resubscribe
            mqttClient.subscribe(MQTT_TOPIC);
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

void readJoystickPosition(float *dx, float *dy) {
    static bool init = true;
    static int calibX = -1;
    static int calibY = -1;
    if (init) {
        init = false;
        digitalWrite(AMUX_S0, LOW);
        calibX = analogRead(A0) - 512;
        digitalWrite(AMUX_S0, HIGH);
        calibY = analogRead(A0) - 512;
    }

    digitalWrite(AMUX_S0, LOW);
    int rx = analogRead(A0);
    digitalWrite(AMUX_S0, HIGH);
    int ry = analogRead(A0);
    *dx = float(rx - 512 - calibX) / -512.0;
    *dy = float(ry - 512 - calibY) / 512.0;
    if (fabs(*dx) < 0.05) *dx = 0;
    if (fabs(*dy) < 0.05) *dy = 0;
}

uint16_t color(uint8_t r, uint8_t g, uint8_t b) {
    // red = 5 bits at 0xF800
    // green = 6 bits green at 0x07E0
    // blue = 5 bits at 0x001F
    return uint16_t(b >> 3) << 11 | uint16_t(g >> 2) << 5 | uint16_t(r >> 3);
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

void colorSpace(float x, float y, uint8_t *r, uint8_t *g, uint8_t *b) {
    float rf, gf, bf;
    if (x < .5) {
        hsv2rgb(y * 360.0, 1, x * 2, &rf, &gf, &bf);
    } else {
        hsv2rgb(y * 360.0, 1 - (x - 0.5) * 2, 1, &rf, &gf, &bf);
    }
    *r = rf * 255;
    *g = gf * 255;
    *b = bf * 255;
}

void renderUI(float cursorX, float cursorY) {
    static bool init = true;
    static int prevIX = -1;
    static int prevIY = -1;

    int ix = cursorX * (display.width() - 4) + 2;
    int iy = cursorY * (display.height() - 4) + 2;
    if (ix == prevIX && iy == prevIY) return;

    display.startWrite();
    if (init) {
        init = false;
        for (int x = 0; x < display.width(); x++) {
            for (int y = 0; y < display.height(); y++) {
                uint8_t r, g, b;
                colorSpace(
                    float(x) / float(display.width()),
                    float(y) / float(display.height()),
                    &r, &g, &b
                );
                display.writePixel(x, y, color(r, g, b));
            }
        }
    }

    // clear previous cursor
    for (int x = 0; x < display.width(); x++) {
        int y = prevIY;
        uint8_t r, g, b;
        colorSpace(
            float(x) / float(display.width()),
            float(y) / float(display.height()),
            &r, &g, &b
        );
        display.writePixel(x, y, color(r, g, b));
    }
    for (int y = 0; y < display.height(); y++) {
        int x = prevIX;
        uint8_t r, g, b;
        colorSpace(
            float(x) / float(display.width()),
            float(y) / float(display.height()),
            &r, &g, &b
        );
        display.writePixel(x, y, color(r, g, b));
    }

    for (int x = 0; x < display.width(); x++) {
        display.writePixel(x, iy, color(0xff, 0xff, 0xff));
    }
    for (int y = 0; y < display.height(); y++) {
        display.writePixel(ix, y, color(0xff, 0xff, 0xff));
    }

    display.endWrite();

    prevIX = ix;
    prevIY = iy;
}

void loop() {
    if (!mqttClient.connected()) {
        reconnectMQTT();
    }

    static float cursorX = .5;
    static float cursorY = .5;

    float dx, dy;
    readJoystickPosition(&dx, &dy);
    cursorX += dx * 0.025;
    cursorY += dy * 0.025;

    if (cursorX < 0.0) cursorX = 0.0;
    if (cursorX > 1.0) cursorX = 1.0;
    cursorY = fmod(cursorY + 1, 1);

    if (dx != 0 || dy != 0) {
        uint8_t r, g, b;
        colorSpace(cursorX, cursorY, &r, &g, &b);

        char buf[7] = {0};
        snprintf(buf, sizeof(buf), "%02x%02x%02x", r, g, b);
        mqttPublish(MQTT_TOPIC, buf);
        delay(10);
    }

    renderUI(cursorX, cursorY);

    mqttClient.loop();
    yield();
}
