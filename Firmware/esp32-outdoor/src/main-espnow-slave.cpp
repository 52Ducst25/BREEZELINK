// ============================================================================
//  BreezeLink - ESP32-C3 - OUTDOOR node - SLAVE role (ESP-NOW)
// ----------------------------------------------------------------------------
//  Uses NO WiFi/MQTT. Reads the DHT and broadcasts an ESP-NOW packet to the
//  indoor gateway; the gateway is what relays it to the cloud on this node's own
//  topic.
//
//  The whole radio layer (scanning for the router's channel, locking onto it,
//  broadcasting) lives in ../../shared/espnow-slave-radio.h -- shared with the 4
//  room-corner nodes. It used to live entirely in this file; it was extracted
//  because the four room nodes need exactly the same thing, and the "loses the
//  channel after every scan" trap has already cost us once, so there should not be
//  two copies of it.
//
//  DHTesp AND NOT Adafruit's DHT library -- mandatory since the board moved to the
//  ESP32-C3 (2026-08-15). The Adafruit version disables interrupts for over a
//  second when no sensor is connected, and the C3 has only ONE core with the
//  WiFi/ESP-NOW stack running right on it. Full reasoning in platformio.ini, in
//  the lib_deps block.
// ============================================================================
#include <Arduino.h>
#include <DHTesp.h>

#include "config.h"
#include "espnow-message.h"
#include "espnow-slave-radio.h"

static DHTesp dht;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n== BreezeLink - ESP32-C3 - OUTDOOR (SLAVE / ESP-NOW) ==");
  dht.setup(DHT_PIN, DHT_TYPE);

  if (!EspNowSlaveRadio::begin(WIFI_SSID)) {
    Serial.println("esp_now_init FAILED - restarting");
    delay(2000);
    ESP.restart();
  }
}

static unsigned long lastSend = 0;

void loop() {
  EspNowSlaveRadio::tickRescan(WIFI_SSID);

  const unsigned long now = millis();
  if (lastSend != 0 && now - lastSend < TELEMETRY_MS) {
    delay(50);
    return;
  }
  lastSend = now;

  // A broken sensor does NOT mean a dead node. Keep sending the heartbeat, just
  // with NaN values -- the gateway reads that as "alive but no reading yet" and
  // keeps the node online instead of wrongly reporting it disconnected. This used
  // to return early, so a DHT failure made the node look dead.
  // DHTesp returns both values in ONE read, unlike Adafruit (two calls, waking
  // the sensor twice). We still check `getStatus()` rather than only isnan: this
  // library reports checksum/timeout errors through it, and a frame with a bad
  // checksum can still produce numbers inside the valid range.
  const TempAndHumidity th = dht.getTempAndHumidity();
  float t = th.temperature;
  float h = th.humidity;
  const bool sensorOk =
      dht.getStatus() == DHTesp::ERROR_NONE && !(isnan(t) || isnan(h));
  if (!sensorOk) {
    Serial.println("Sensor read error (NaN) - still sending the heartbeat, check the DHT wiring");
    t = NAN;
    h = NAN;
  }

  AcEspNowPacket pkt;
  acEspNowFill(&pkt, DEVICE_UUID, t, h, AC_NODE_OUTDOOR, AC_CORNER_NONE);
  const bool sent = EspNowSlaveRadio::broadcast(&pkt, sizeof(pkt));

  if (sensorOk) {
    Serial.printf("[espnow] t=%.1f h=%.0f channel=%d -> %s\n",
                  t, h, EspNowSlaveRadio::channel(), sent ? "sent" : "RADIO ERROR");
  } else {
    Serial.printf("[espnow] heartbeat (no reading yet) channel=%d -> %s\n",
                  EspNowSlaveRadio::channel(), sent ? "sent" : "RADIO ERROR");
  }
}
