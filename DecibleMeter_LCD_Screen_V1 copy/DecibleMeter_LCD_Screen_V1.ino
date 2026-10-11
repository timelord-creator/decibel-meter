/*
====================================================================

  ESP32-S3 SOUND LEVEL METER
  GC9A01 Round Display
  INMP441 I2S MEMS Microphone

  Author: Will A
  Version: 1.0
  Date: October 10 2026

  Features:
    - 2 second gauge averaging
    - 5 second numeric averaging
    - 30 second average marker
    - Peak hold with reset button
    - I2S microphone input
    - GC9A01 round TFT display

====================================================================
*/
#include <Arduino_GFX_Library.h>
#include "ESP_I2S.h"

// ---------- Screen setup ----------
Arduino_DataBus *bus = new Arduino_ESP32SPI(
  9  /* DC */,
  10 /* CS */,
  12 /* SCK */,
  11 /* MOSI */,
  GFX_NOT_DEFINED /* MISO */
);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, 14 /* RST */, 0 /* rotation */, true /* IPS */);

// ---------- Mic setup ----------
I2SClass i2s;
const int SAMPLE_BUFFER_SIZE = 2048;
int32_t sampleBuffer[SAMPLE_BUFFER_SIZE];
static double smoothedDb = 40;
double lastRawRms = 0;
static double lastGoodDb = 40;

double readDb() {

  size_t bytesRead =
      i2s.readBytes((char *)sampleBuffer, sizeof(sampleBuffer));

  int samples = bytesRead / sizeof(int32_t);

  if (samples < 10) {
    return smoothedDb;
  }

  // -------------------------
  // Calculate DC offset
  // -------------------------
  double mean = 0;

  for (int i = 0; i < samples; i++) {
    mean += (sampleBuffer[i] >> 8);
  }

  mean /= samples;

  // -------------------------
  // RMS with outlier rejection
  // -------------------------
  double sumSquares = 0;
  int validSamples = 0;

  for (int i = 0; i < samples; i++) {

    double s = (sampleBuffer[i] >> 8) - mean;

    // reject obviously bad spikes
    if (fabs(s) > 500000) {
      continue;
    }

    sumSquares += s * s;
    validSamples++;
  }

  if (validSamples < 10) {
    return smoothedDb;
  }

  double rms = sqrt(sumSquares / validSamples);

  lastRawRms = rms;

  double db = 20.0 * log10(rms + 1);

  // calibration
  double dbSPL = db - 51;

  // -------------------------
  // Spike rejection
  // -------------------------
  if (fabs(dbSPL - lastGoodDb) > 12) {
    dbSPL = lastGoodDb;
  } else {
    lastGoodDb = dbSPL;
  }

  // -------------------------
  // Heavy smoothing
  // -------------------------
  smoothedDb =
      (0.95 * smoothedDb) +
      (0.05 * dbSPL);

  return smoothedDb;
}

// ---------- Global values ----------
double maxDb = 0;
double gaugeDb = 40;
double displayDb = 40;
double avg30s = 0;
bool   avgReady = false;

unsigned long resetDisplayUntil = 0;
bool resetMessageShowing = false;
bool needsRedraw = false;

// Averaging accumulators
double sumFor30s = 0;
int    samplesFor30s = 0;
double sumFor5s = 0;
int    samplesFor5s = 0;
double sumFor2s = 0;
int    samplesFor2s = 0;

// Startup settle time (ignore first 4 seconds)
unsigned long bootTime = 0;
const unsigned long SETTLE_MS = 4000;

// ---------- Timing ----------
unsigned long lastGaugeUpdate = 0;
const unsigned long gaugeInterval = 2000;

unsigned long lastNumberUpdate = 0;
const unsigned long numberInterval = 5000;

unsigned long lastAvgUpdate = 0;
const unsigned long avgInterval = 30000UL;

unsigned long lastHeartbeat = 0;  // diagnostic only, safe to remove later

// ---------- Reset button (interrupt-driven) ----------
const int RESET_BUTTON_PIN = 2;
volatile bool resetRequested = false;       // "volatile" = may change at any moment, outside normal code flow
volatile unsigned long lastIsrTime = 0;

// IRAM_ATTR keeps this function in the chip's fastest internal memory so it can run instantly
void IRAM_ATTR onResetButton() {
  unsigned long now = millis();
  if (now - lastIsrTime > 250) {  // debounce: ignore the rapid on/off chatter of a mechanical switch
    resetRequested = true;
    lastIsrTime = now;
  }
}

void doReset(double currentValue) {
  maxDb     = currentValue;
  avg30s    = currentValue;
  avgReady  = true;
  gaugeDb   = currentValue;
  displayDb = currentValue;

  sumFor30s = 0;  samplesFor30s = 0;
  sumFor5s  = 0;  samplesFor5s  = 0;
  sumFor2s  = 0;  samplesFor2s  = 0;

  lastAvgUpdate = millis();  // restart the 30-second average window cleanly

  resetDisplayUntil = millis() + 1500;
  resetMessageShowing = true;
  needsRedraw = true;
  Serial.println("MAX + AVG reset!");
}

// ---------- Gauge drawing ----------
void drawGauge(double gaugeValue, double numberValue, double maxValue, double avg30s, bool avgReady);

const float GAUGE_START = 225.0;
const float GAUGE_END   = 495.0;
const int16_t R_OUTER = 115;
const int16_t R_INNER = 90;
const double DB_MIN = 30.0;
const double DB_MAX = 100.0;

void fillArc(int16_t cx, int16_t cy, float startDeg, float endDeg,
             int16_t rOuter, int16_t rInner, uint16_t color, float step = 1.0) {
  for (float a = startDeg; a < endDeg; a += step) {
    float a1 = a;
    float a2 = min(a + step, endDeg);
    float rad1 = radians(a1);
    float rad2 = radians(a2);

    int16_t xo1 = cx + rOuter * sin(rad1);
    int16_t yo1 = cy - rOuter * cos(rad1);
    int16_t xo2 = cx + rOuter * sin(rad2);
    int16_t yo2 = cy - rOuter * cos(rad2);
    int16_t xi1 = cx + rInner * sin(rad1);
    int16_t yi1 = cy - rInner * cos(rad1);
    int16_t xi2 = cx + rInner * sin(rad2);
    int16_t yi2 = cy - rInner * cos(rad2);

    gfx->fillTriangle(xo1, yo1, xo2, yo2, xi1, yi1, color);
    gfx->fillTriangle(xo2, yo2, xi1, yi1, xi2, yi2, color);
  }
}

void drawLineAtValue(double val, uint16_t color) {
  if (val < DB_MIN) return;

  double clamped = constrain(val, DB_MIN, DB_MAX);
  float angle = GAUGE_START + (GAUGE_END - GAUGE_START) *
                ((clamped - DB_MIN) / (DB_MAX - DB_MIN));
  float rad = radians(angle);

  int16_t x1 = 120 + (R_INNER - 35) * sin(rad);
  int16_t y1 = 120 - (R_INNER - 35) * cos(rad);
  int16_t x2 = 120 + (R_OUTER + 2) * sin(rad);
  int16_t y2 = 120 - (R_OUTER + 2) * cos(rad);

  gfx->drawLine(x1, y1, x2, y2, color);
  gfx->drawLine(x1 + 1, y1, x2 + 1, y2, color);
}

void drawGauge(double gaugeValue, double numberValue, double maxValue, double avg30s, bool avgReady) {
  fillArc(120, 120, GAUGE_START, GAUGE_END, R_OUTER + 8, 40, 0x0000, 3.0);
  fillArc(120, 120, GAUGE_START, GAUGE_END, R_OUTER, R_INNER, 0x2104, 1.5);

  double clamped = constrain(gaugeValue, DB_MIN, DB_MAX);
  float valueAngle = GAUGE_START + (GAUGE_END - GAUGE_START) *
                     ((clamped - DB_MIN) / (DB_MAX - DB_MIN));

  uint16_t fillColor = 0x07E0;
  if      (clamped > 85) fillColor = 0xF800;
  else if (clamped > 65) fillColor = 0xFFE0;

  fillArc(120, 120, GAUGE_START, valueAngle, R_OUTER, R_INNER, fillColor, 1.5);

  if (avgReady) drawLineAtValue(avg30s, 0xFFE0);
  drawLineAtValue(maxValue, 0xF800);

  gfx->fillRect(50,  92, 140, 38, 0x0000);
  gfx->fillRect(20, 180, 200, 55, 0x0000);

  if (millis() < resetDisplayUntil) {
    gfx->setTextColor(0xF800);
    gfx->setTextSize(3);
    gfx->setCursor(55, 105);
    gfx->print("RESET");
  } else {
    gfx->setTextColor(0xFFFF);
    gfx->setTextSize(3);
    gfx->setCursor(70, 100);
    gfx->print(numberValue, 1);

    gfx->setTextSize(2);
    gfx->setCursor(95, 130);
    gfx->print("dB");
  }

  gfx->setTextSize(2);
  gfx->setTextColor(0xF800);
  gfx->setCursor(55, 185);
  gfx->print("MAX ");
  gfx->print(maxValue, 1);

  gfx->setTextSize(2);
  gfx->setTextColor(0xFFE0);
  gfx->setCursor(70, 210);
  gfx->print("AVG ");
  if (avgReady) gfx->print(avg30s, 1);
  else          gfx->print("---");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("BOOT OK");

  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  // FALLING = the moment the pin goes from HIGH to LOW, i.e. the instant the button connects it to GND
  attachInterrupt(digitalPinToInterrupt(RESET_BUTTON_PIN), onResetButton, FALLING);

  if (!gfx->begin()) Serial.println("Display init failed!");
  else               Serial.println("Display init OK");
  gfx->fillScreen(0x0000);

  i2s.setPins(4, 5, -1, 6, -1);
  if (!i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("I2S init failed!");
  } else {
    Serial.println("I2S init OK");
  }

  bootTime = millis();
  delay(500);
  drawGauge(40, 40, 40, 0, false);
  lastGaugeUpdate  = millis();
  lastNumberUpdate = millis();
  lastAvgUpdate    = millis();
}

void loop() {
  double dB = readDb();

  bool settled = (millis() - bootTime > SETTLE_MS);

  if (settled) {
    if (dB > maxDb) {
  maxDb = dB;
}
else {
  maxDb -= 0.002;

  if (maxDb < dB)
    maxDb = dB;
}

    sumFor2s  += dB;  samplesFor2s++;
    sumFor5s  += dB;  samplesFor5s++;
    sumFor30s += dB;  samplesFor30s++;
  }

  // Reset requested by the button interrupt
  if (resetRequested) {
    resetRequested = false;
    if (settled) doReset(dB);
  }

  // 30-second average
  if (settled && millis() - lastAvgUpdate >= avgInterval) {
    if (samplesFor30s > 0) {
      avg30s = sumFor30s / samplesFor30s;
      avgReady = true;
    }
    sumFor30s = 0;
    samplesFor30s = 0;
    lastAvgUpdate = millis();
  }

  // Gauge value every 2 seconds
  if (millis() - lastGaugeUpdate >= gaugeInterval) {
    if (samplesFor2s > 0) {
      gaugeDb = sumFor2s / samplesFor2s;
    }
    sumFor2s = 0;
    samplesFor2s = 0;
    needsRedraw = true;
    lastGaugeUpdate = millis();
  }

  // Center number every 5 seconds
  if (millis() - lastNumberUpdate >= numberInterval) {
    if (samplesFor5s > 0) {
      displayDb = sumFor5s / samplesFor5s;
    }
    sumFor5s = 0;
    samplesFor5s = 0;
    needsRedraw = true;

    Serial.print("5s: ");
    Serial.print(displayDb, 1);
    Serial.print("  Max: ");
    Serial.print(maxDb, 1);
    Serial.print("  30s AVG: ");
    if (avgReady) Serial.println(avg30s, 1);
    else          Serial.println("---");

    lastNumberUpdate = millis();
  }

  // Clear the RESET message as soon as it expires
  if (resetMessageShowing && millis() >= resetDisplayUntil) {
    resetMessageShowing = false;
    needsRedraw = true;
  }

  // ONE redraw per pass, no matter how many things asked for it
  if (needsRedraw) {
    drawGauge(gaugeDb, displayDb, maxDb, avg30s, avgReady);
    needsRedraw = false;
  }

  // Diagnostic heartbeat (remove once everything is stable)
  if (millis() - lastHeartbeat >= 1000) {
   if (millis() - lastHeartbeat >= 1000) {

  Serial.print("dB=");
  Serial.print(dB, 1);

  Serial.print(" RMS=");
  Serial.print(lastRawRms, 0);

  Serial.print(" MAX=");
  Serial.println(maxDb, 1);

  lastHeartbeat = millis();
}
    lastHeartbeat = millis();
  }
}