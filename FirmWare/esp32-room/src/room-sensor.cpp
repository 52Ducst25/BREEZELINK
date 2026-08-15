#include "room-sensor.h"

#include <DHTesp.h>

namespace RoomSensor {
namespace {

DHTesp        g_dht;
unsigned long g_lastReadMs = 0;
bool          g_started = false;

float   g_temp = NAN, g_humidity = NAN;
uint8_t g_fails = 0;

}  // namespace

void begin(uint8_t pin) {
  // DHTesp AND NOT Adafruit's DHT library -- the same reason recorded in
  // platformio.ini's lib_deps: the Adafruit version disables interrupts for over
  // a second when no sensor is connected and panics the core. On an ESP32-C3 (a
  // single core, with the BLE stack running right on it) the consequences are far
  // worse than on a dual-core ESP32: losing interrupts for a second makes the BLE
  // stack miss its schedule and breaks connections/scans.
  g_dht.setup(pin, DHTesp::DHT22);
  g_started = true;
  g_lastReadMs = 0;
}

bool poll() {
  if (!g_started) return false;
  if (g_lastReadMs != 0 && millis() - g_lastReadMs < READ_PERIOD_MS) return false;
  g_lastReadMs = millis();

  // Take both values in one round -- do NOT call getTemperature() and then
  // getHumidity() as two separate commands: each command is a handshake with the
  // sensor, and since the DHT22's minimum interval is 2s the second command just
  // returns the previous value.
  const TempAndHumidity th = g_dht.getTempAndHumidity();
  const bool ok = g_dht.getStatus() == DHTesp::ERROR_NONE &&
                  !isnan(th.temperature) && !isnan(th.humidity);

  if (ok) {
    g_temp = th.temperature;
    g_humidity = th.humidity;
    g_fails = 0;
    return true;
  }

  if (g_fails < 0xFF) g_fails++;
  if (g_fails >= FAIL_LIMIT) {
    // Clearing the old values is deliberate. Holding onto a reading from 20
    // seconds ago and continuing to broadcast it makes the node lie convincingly:
    // the gateway sees a room corner that is "alive, with a stable temperature"
    // at the exact moment its sensor wire has come off.
    g_temp = NAN;
    g_humidity = NAN;
  }
  return true;
}

bool read(float &tempC, float &humidity) {
  if (isnan(g_temp) || isnan(g_humidity)) return false;
  tempC = g_temp;
  humidity = g_humidity;
  return true;
}

uint8_t consecutiveFailures() { return g_fails; }

}  // namespace RoomSensor
