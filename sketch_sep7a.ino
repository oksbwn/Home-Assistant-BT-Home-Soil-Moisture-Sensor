#include <BtHomeV2Device.h>
#include "NimBLEDevice.h"
#include "esp_system.h"
#include <Adafruit_NeoPixel.h>
#include <math.h>

#define BAT_ADC_PIN 1
#define R1 1000000.0
#define R2 1100000.0
#define MOIST_ADC_PIN 2
#define MOIST_PWR_PIN 4

#define LED_PIN 8
#define NUM_LEDS 1

Adafruit_NeoPixel led(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

BtHomeV2Device btHome("SoilHealthMini", "Soil Moisture Sensor V1", false);
NimBLEAdvertising *pAdvertising;

void setup() {
  led.begin();
  led.show();

  pinMode(MOIST_PWR_PIN, OUTPUT);
  digitalWrite(MOIST_PWR_PIN, LOW);

  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  analogSetPinAttenuation(MOIST_ADC_PIN, ADC_11db);

  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_EXT || reason == ESP_RST_POWERON) {
    for (int t = 0; t < 20000; t += 50) {
      float brightness = (sin(t * 3.14159 / 20000.0 * 2.0) * 127.5) + 127.5;
      led.setPixelColor(0, led.Color((int)brightness, 0, 0));
      led.show();
      delay(50);
    }
    led.setPixelColor(0, led.Color(0, 0, 0));
    led.show();
  }

  NimBLEDevice::init("");
  pAdvertising = NimBLEDevice::getAdvertising();

  long mv = readBat_mV();
  int batPct = socFromVoltage_mV(mv);
  int moistPct = readMoisturePercent();

  if (moistPct >= 0) btHome.addMoisturePercent_Resolution_0_01(moistPct);
  btHome.addBatteryPercentage(batPct);

  uint8_t advertisementData[MAX_ADVERTISEMENT_SIZE];
  size_t size = btHome.getAdvertisementData(advertisementData);
  NimBLEAdvertisementData pAdvData;
  std::vector<uint8_t> data(advertisementData, advertisementData + size);
  pAdvData.addData(data);

  pAdvertising->setAdvertisementData(pAdvData);
  pAdvertising->setConnectableMode(0);

  // Send multiple packets before sleep
  for (int i = 0; i < 3; i++) {
    pAdvertising->start();
    delay(1000);
    pAdvertising->stop();
    delay(200);
  }

  const uint64_t SLEEP_TIME = 5ULL * 60ULL * 1000000ULL;
  esp_deep_sleep(SLEEP_TIME);
}

void loop() {}

long readBat_mV() {
  int raw = 0;
  for (int i = 0; i < 16; i++) raw += analogRead(BAT_ADC_PIN);
  float avg = raw / 16.0f;
  float mv_at_pin = (avg / 4095.0f) * 3550.0f;   // ADC full scale at 11dB ≈ 3.55V
  return (long)(mv_at_pin * (R1 + R2) / R2);
}

int socFromVoltage_mV(long mv) {
  struct Pt { int mv; int pct; } curve[] = {
    {4200,100},{4100,90},{4000,80},{3950,75},{3900,70},{3850,60},
    {3800,55},{3750,45},{3700,35},{3650,25},{3600,15},{3500,7},
    {3400,3},{3300,0}
  };
  if (mv >= curve[0].mv) return 100;
  if (mv <= curve[13].mv) return 0;
  for (int i = 0; i < 13; i++) {
    if (mv <= curve[i].mv && mv >= curve[i + 1].mv) {
      float x0 = curve[i + 1].mv, x1 = curve[i].mv;
      float y0 = curve[i + 1].pct, y1 = curve[i].pct;
      return (int)(y0 + (mv - x0) * (y1 - y0) / (x1 - x0) + 0.5f);
    }
  }
  return 0;
}

int readMoisturePercent() {
  digitalWrite(MOIST_PWR_PIN, HIGH);
  delay(200);
  long sum = 0;
  for (int i = 0; i < 10; i++) sum += analogRead(MOIST_ADC_PIN);
  digitalWrite(MOIST_PWR_PIN, LOW);
  int raw = sum / 10;

  const int airValue = 3500;
  const int waterValue = 1200;

  if (raw < 500 || raw > 4000) return -1;

  int percent = map(raw, airValue, waterValue, 0, 100);
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  return percent;
}
