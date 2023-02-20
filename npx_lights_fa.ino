/** 
* This code was used during the TU Wien Chor concert of 2022.
* Written for ESP32, but could probably be used on 8266 as well.
* Code is like super cleaned up (not), we finished this on the day of concert.
*
* Works with Wifi Mosquitto Broker. Bluetooth not supported yet.
* 
* Author: Ulrich Aschl (@hovercat)
* 
* Beautiful pride animation borrowed from FastLED-library examples
* https://github.com/FastLED/FastLED/blob/master/examples/Pride2015/Pride2015.ino
* 
*/
#include <Esp.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "FastLED.h"
#include "npx_lights_fa.h"

// USER DEFINED VALUES
#define LED_TYPE NEOPIXEL
#define DATA_PIN 15
#define NUM_LEDS 8 // number of leds on data pin
#define BT_PIN 33 // unfinished
#define WIFI_PIN 27
int const ESP_ID = 1;

// WIFI
const char* ssid = "<<<WIFI_SSID>>>";
const char* password = "<<<WIFI_KEY>>>";
// MQTT
WiFiClient wifi_client;
PubSubClient client(wifi_client);
const char* mqtt_broker = "<<<<MQTT.BROKER.IP.STRING>>>>"

// Bluetooth
//WiFiClient wifi_client;
//PubSubClient client(wifi_client);
//const char* mqtt_broker = "192.168.1.1";


int pings = 0;
char ping_str[10];
char ping_msg[6];
long ms;

// MQTT messages sent to this device
#define SETUP_RESTART "setup/restart"
#define L_SHOW "show"
#define L_COLOR_HSV "color_hsv"
#define L_COLOR_HSV_MAX "color_hsv/max"
#define L_COLOR_HSV_ADD "color_hsv/add"
#define L_MODE "mode"
#define L_MODE_PAUSE "mode/pause"
#define L_MODE_GO "mode/go"
#define L_MODE_NEXT_FRAME "mode/next_frame"
#define L_MODE_SPEED "mode/speed"
#define L_PULSE_RANGE "pulse/range"



CRGB leds[NUM_LEDS];

int mq_active_mode = -1;
bool mq_go = 1;
bool mq_frame = 1;
int mq_speed = 300; // ms
int hue_change_steps;
static CHSV hue1;
static uint8_t mq_hsv_hue = 160;
static uint8_t mq_hsv_saturation = 255;
static uint8_t mq_hsv_value = 255;
static uint8_t mq_pulse_hue_range = 20;
static uint16_t mq_anim_delay = 500;

static uint8_t mq_hsv_value_MAX = 255;

static uint16_t sHue16 = mq_hsv_hue * 256;
static uint16_t sHueRange16 = 0;
static long pingMillis = 0;
static long sLastMillis = 0;

#define M_STILL 0
#define M_GRADIENT 1
#define M_A_FIRST 20
#define M_A_STARTUP 20
#define M_A_PRIDE 21
#define M_A_DISCO 22
#define M_A_PULSE 23
#define M_A_SPEED_UP 24
#define M_A_SCHNECKE 25
#define M_A_LAST 25

int bt = 0;
int wifi = 0;

void setup() {
  sprintf(ping_str, "online/%i", ESP_ID);
  // put your setup code here, to run once:
  Serial.begin(115200);

  pinMode(BT_PIN, INPUT);
  pinMode(WIFI_PIN, INPUT);
  bt = digitalRead(BT_PIN);
  wifi = digitalRead(WIFI_PIN);
  Serial.print("Bluetooth: ");
  Serial.println(bt);
  Serial.print("Wifi: ");
  Serial.println(wifi);

  // connect to wifi
  Serial.print("Starting up ESP32 and connecting to WIFI ");
  Serial.print(ssid);
  setup_wifi();

  // connect to mqtt broker
  Serial.println("Connecting to MQTT Server");
  client.setServer(mqtt_broker, 1883);
  client.setCallback(callback);

  // setting up neopixels
  Serial.println("Setting up Neopixels");
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);

  mq_active_mode = M_STILL;
  mq_hsv_hue = 160;
  mq_hsv_saturation = 255;
  mq_hsv_value = 0;
  hue1 = CHSV(mq_hsv_hue, mq_hsv_saturation, mq_hsv_value);
  Serial.println("All set up, ready to go!");
}

void reconnect() {
    char subscr_str[4];
    sprintf(subscr_str, "%i/#", ESP_ID);
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(subscr_str)) {
      Serial.println("connected");
      Serial.println("Subscribing to topics");
      // Subscribe
      client.subscribe("setup/#");
      client.subscribe("a/#");
      client.subscribe(subscr_str);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup_wifi() {
  int i = 0;
  WiFi.begin(ssid, password);
  WiFi.setSleep(false); // TODO STROM
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    i = i + 1;
    if (i >= 20) {
      Serial.println(" => Restarting ESP32 - can't connect to Wifi");
      ESP.restart();
    }
  }
  Serial.println(" => WiFi connected");
}



void loop() {
  delay(1);
  if (!client.connected()) {
    reconnect();
  }
    client.loop();

  ms = millis();
  if (ms-pingMillis > 5000) {
    sprintf(ping_msg, "%i", pings);
    client.publish(ping_str, ping_msg);
    pings++;
    pingMillis = ms;
  }

  switch (mq_active_mode) {
    case M_STILL:
        still();
        break;
    case M_GRADIENT:
        gradient();
        break;
    case M_A_PRIDE:
        pride();
        break;
    case M_A_DISCO:
        disco();
        break;
    case M_A_SCHNECKE:
        schnecke();
        break;
    case M_A_PULSE:
        pulse();
        break;
    case M_A_SPEED_UP:
        speed_up_single_pixel();
        break;
    default:
        break;
  }

  if (mq_go || mq_frame) {
    FastLED.show();
    mq_frame = 0;
  }
}

int str_array_size(char *str) {
  int count = 1;
  const char *tmp = str;
  while(tmp = strstr(tmp, ";")) {
    count++;
    tmp++;
  }
  return count;
}

void callback(char* topic, byte* message, unsigned int length) {
    // Message handling
    Serial.print("MQTT Callback:\t");
    Serial.print(topic);
    Serial.print("\t");

    // get command
    bool is_setup = 0;
    char topic_substr = topic[0];
    if (topic_substr == 's') {
        is_setup = 1;
    } else {
        topic = topic+2;
    }
    Serial.print(topic);
    Serial.println("\t");

    // cut off \n if there is one
    while (message[length-1] == '\n') {
        length--;
    }
    // convert msg to string
    char msg[length];
    memcpy(msg, message, length);
    msg[length] = '\0';
    Serial.println(msg);
    // find out if it is array:
    int count = str_array_size(msg);
    Serial.println(count);
    // convert msg to string array
    char* msg_arr[count];
    Serial.println("Array contents: ");
    if (count == 1) {
        Serial.println(msg);
        //memcpy(msg_arr[i], msg, length);
        msg_arr[0] = msg;
    }
    if (count > 1) {
        char* pch;
        int i = 0;
        pch = strtok(msg, ";");
        while (pch != NULL) {
            Serial.println(pch);
            //strcpy(*(msg_arr+i), pch);
            msg_arr[i] = pch;
            pch = strtok(NULL, ";");
            i++;
        }
    }
    int i_msg;
    Serial.println("Received message. Checking now against procedures.");

    char *nptr = nullptr;
    if (strcmp(topic, L_SHOW) == 0) {
        FastLED.show();
        return;
    }
    if (strcmp(topic, L_COLOR_HSV) == 0) {
        // will take msgs in form of "hue;sat;val"
        if (count > 0 && strtol(msg_arr[0], &nptr, 10) != -1) {
            mq_hsv_hue = strtol(msg_arr[0], &nptr, 10);
            sHue16 = mq_hsv_hue * 256;
        };
        if (count > 1 && strtol(msg_arr[1], &nptr, 10) != -1) { mq_hsv_saturation = strtol(msg_arr[1], &nptr, 10); };
        if (count > 2 && strtol(msg_arr[2], &nptr, 10) != -1) {
            mq_hsv_value = strtol(msg_arr[2], &nptr, 10);
            mq_hsv_value = mq_hsv_value > mq_hsv_value_MAX ? mq_hsv_value_MAX : mq_hsv_value;
        };
        hue1 = CHSV(mq_hsv_hue, mq_hsv_saturation, mq_hsv_value);
    }
    if (strcmp(topic, L_COLOR_HSV_ADD) == 0) {
        if (count > 0) {
            mq_hsv_hue += strtol(msg_arr[0], &nptr, 10);
            sHue16 += strtol(msg_arr[0], &nptr, 10) * 256;
            //sHue16 = strtol(msg_arr[0], &nptr, 10) * 256;
        };
        if (count > 1) {
            if (strtol(msg_arr[1], &nptr, 10) > 0) { mq_hsv_saturation = qadd8(mq_hsv_saturation, strtol(msg_arr[1], &nptr, 10));
            } else { mq_hsv_saturation = qsub8(mq_hsv_saturation, -1 * strtol(msg_arr[1], &nptr, 10)); };
            ///mq_hsv_saturation = mq_hsv_saturation <= 0 ? 0 : mq_hsv_saturation;
        }
        if (count > 2) {
            if (strtol(msg_arr[2], &nptr, 10) > 0) { mq_hsv_value = qadd8(mq_hsv_value, strtol(msg_arr[2], &nptr, 10));
            } else { mq_hsv_value = qsub8(mq_hsv_value, -1 * strtol(msg_arr[2], &nptr, 10)); };

            mq_hsv_value = mq_hsv_value > mq_hsv_value_MAX ? mq_hsv_value_MAX : mq_hsv_value;
        }
        hue1 = CHSV(mq_hsv_hue, mq_hsv_saturation, mq_hsv_value);
    }
    if (strcmp(topic, L_COLOR_HSV_MAX) == 0) {
        mq_hsv_value_MAX = strtol(msg_arr[0], &nptr, 10);
        if (mq_hsv_value > mq_hsv_value_MAX) {
            mq_hsv_value = mq_hsv_value_MAX;
        }
        hue1 = CHSV(mq_hsv_hue, mq_hsv_saturation, mq_hsv_value);
    }

    if (strcmp(topic, L_MODE) == 0) {
        Serial.println("mode");
        mq_active_mode = strtol(msg, &nptr, 10);
        return;
    }
    if (strcmp(topic, L_MODE_GO) == 0) {
        mq_go = 1;
        return;
    }
    if (strcmp(topic, L_MODE_PAUSE) == 0) {
        mq_go = 0;
        return;
    }
    if (strcmp(topic, L_MODE_NEXT_FRAME) == 0) {
        mq_frame = 1;
        return;
    }
    if (strcmp(topic, L_MODE_SPEED) == 0) {
        i_msg = strtol(msg, &nptr, 10);
        mq_speed = i_msg;
        return;
    }
    if (strcmp(topic, L_PULSE_RANGE) == 0) {
        Serial.println("pulse range");
        i_msg = strtol(msg, &nptr, 10);
        mq_pulse_hue_range = i_msg;
        sHueRange16 = mq_pulse_hue_range * 256;
        return;
    }



    if (is_setup == 0) {
        return;
    }

    if (strcmp(topic, SETUP_RESTART) == 0) {
        Serial.print("Restarting ESP32");
        ESP.restart();
    }

}


// ANIMATIONS GO FROM HERE

void still() {
    fill_solid(leds, NUM_LEDS, hue1);
}

void gradient() {
    CHSV hue2 = CHSV(hue1.h, hue1.s / 4, hue1.v / 6);
    fill_gradient(leds, NUM_LEDS, hue1, hue2);
}

void pride() // check https://github.com/FastLED/FastLED/blob/master/examples/Pride2015/Pride2015.ino
{
  if (!mq_go && !mq_frame) { return; }
  static uint16_t sPseudotime = 0;

  uint8_t sat8 = beatsin88( 87, 220, 250);
  uint8_t brightdepth = beatsin88( 341, 96, 224);
  uint16_t brightnessthetainc16 = beatsin88( 203, (25 * 256), (40 * 256));

  uint16_t hue16 = sHue16;//gHue * 256;
  uint16_t hueinc16 = beatsin88(113, 1, 3000);


  uint8_t msmultiplier = beatsin88(147, 23, 60);
  uint16_t deltams = ms - sLastMillis ;
  sLastMillis  = ms;
  sPseudotime += deltams * msmultiplier;
  sHue16 += deltams * beatsin88( 400, 5,9);
  uint16_t brightnesstheta16 = sPseudotime;

  for( uint16_t i = 0 ; i < NUM_LEDS; i++) {
    hue16 += hueinc16;
    uint8_t hue8 = hue16 / 256;

    brightnesstheta16  += brightnessthetainc16;
    uint16_t b16 = sin16( brightnesstheta16  ) + 32768;

    uint16_t bri16 = (uint32_t)((uint32_t)b16 * (uint32_t)b16) / 65536;
    uint8_t bri8 = (uint32_t)(((uint32_t)bri16) * brightdepth) / 65536;
    bri8 += (255 - brightdepth);

    CRGB newcolor = CHSV( hue8, sat8, bri8);

    uint16_t pixelnumber = i;
    pixelnumber = (NUM_LEDS-1) - pixelnumber;

    if (mq_go || mq_frame) {
        nblend( leds[pixelnumber], newcolor, 64);
    }
  }
}

void disco() {
  if ((!mq_go && !mq_frame) || (ms < sLastMillis + mq_speed && !mq_frame)) {
    return;
  }
  sLastMillis = ms;


  for( uint16_t i = 0 ; i < NUM_LEDS; i++) {
    leds[i] = CHSV(
        random8(),
        random8() * 0.5 + 128,
        (random8() * 0.5 + 128) * ((1.0*mq_hsv_value)/255)
    );
  }
}

void schnecke() {
  if ((!mq_go && !mq_frame) || (ms < sLastMillis + mq_speed && !mq_frame)) {
    return;
  }
  sLastMillis = ms;

  static CHSV new_color;
  static int cnt = 0;
  if (cnt == 0) {
      new_color = CHSV(
            random8(),
            random8() * 0.3 + 255 * (1-0.3),
            (random8() * 0.2 + 255 * (1-0.2)) * ((1.0*mq_hsv_value)/255)
      );
  }
  cnt++;
  if (cnt == 3) {
    cnt = 0;
  }

  for( uint16_t i = NUM_LEDS ; i >= 1; i--) {
    leds[i] = leds[i-1];
  }
  leds[0] = new_color;
}

void pulse()
{
  if (!mq_go && !mq_frame) { return; }
  static uint16_t sBrightnesstheta = 0;
  sBrightnesstheta = beatsin16(60, 1, 255*mq_hsv_value);

   uint8_t bri8 = sBrightnesstheta / 256;
   bri8 = bri8 * ((1.0 * mq_hsv_value_MAX) /255);

  for( uint16_t i = 0 ; i < NUM_LEDS; i++) {
    leds[NUM_LEDS-i-1] = CHSV(mq_hsv_hue, mq_hsv_saturation, bri8);
  }
}

void speed_up_single_pixel()
{
  if ((!mq_go && !mq_frame) || (ms < sLastMillis + mq_speed && !mq_frame)) {
    return;
  }
  sLastMillis = ms;
  mq_hsv_hue+=1;
  static int pxl = 0;
  pxl = pxl & (NUM_LEDS-1);
  for (int i = 0; i < NUM_LEDS; i++) {
    if (i == pxl) {
        leds[i] = CHSV(mq_hsv_hue, mq_hsv_saturation, mq_hsv_value);
    } else {
        leds[i] = CHSV(mq_hsv_hue, mq_hsv_saturation, 0);
    }
  }
  pxl++;
}
