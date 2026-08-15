// ============================================================================
//  BreezeLink - ESP32-C3-DevKitM-1 - ROOM-CORNER SENSOR node
// ----------------------------------------------------------------------------
//  Four boards like this one sit in the four corners of a room. Each board does
//  exactly one thing: read a DHT22 and broadcast an ESP-NOW packet to the gateway
//  mounted near the air conditioner.
//
//  NO WiFi, NO MQTT, NO credentials here at all. The gateway publishes on its
//  behalf, exactly the pattern already used for the outdoor node -- and the whole
//  radio layer is shared with it in one file
//  (../../shared/espnow-slave-radio.h).
//
//  WHY ESP-NOW AND NOT BLE:
//    an ESP-NOW frame carries 250 bytes, so it can carry the node's own
//    32-character device_uuid directly -- the gateway simply publishes to that
//    node's topic with no lookup table at all. A classic BLE advertising packet
//    is only 31 bytes and cannot carry the uuid, which would force the gateway to
//    keep an id->uuid table and BE REFLASHED every time a room node is added or
//    removed. Bluetooth in this system is reserved for the gateway <-> Arduino
//    UNO Q link, where both sides have a real GATT connection and no 31-byte
//    ceiling.
//
//  FOUR BOARDS SHARING A CORNER NUMBER IS HARMLESS: `corner` is only a display
//  label on the local screen, while the real identity is DEVICE_UUID -- which
//  already differs because each board gets its own devices row on the web UI. A
//  duplicate label just means the screen prints "corner 1" twice; nobody's
//  readings are lost.
// ============================================================================
#include <Arduino.h>

#include "config.h"
#include "espnow-message.h"
#include "espnow-slave-radio.h"
#include "room-sensor.h"

/// Send interval (ms). DIFFERENT from the sensor read interval (2.5s): read often
/// to filter noise, send less often because room temperature does not change over
/// a few seconds and every packet sent is one telemetry row in Postgres.
///
/// 15s matched the gateway's TELEMETRY_MS.
///
/// Can be forced faster while debugging with `-D ROOM_PUBLISH_MS=3000`: measuring
/// packet loss at a 15s cadence takes minutes to gather enough samples, and while
/// chasing a radio problem a long wait destroys any ability to try things.
/// Do NOT use the fast cadence in production -- every packet is a telemetry row in
/// Postgres.
///
/// 5 SECONDS, NOT 15. This is a HEARTBEAT, not a data-logging cadence: the gateway
/// already throttles the push to the cloud at 15s
/// (SlaveWatch::RELAY_INTERVAL_MS), so broadcasting more often adds NO extra
/// telemetry rows in Postgres.
///
/// Why it has to be frequent: ESP-NOW broadcast has no ACK, so a dropped packet
/// is gone for good, and the gateway has to share the 2.4GHz band with WiFi +
/// Bluetooth so the loss rate is significant (measured: about two thirds of
/// packets lost with BLE enabled). At a 15s cadence with SlaveWatch's 20s
/// disconnection threshold, LOSING A SINGLE PACKET marks the node as
/// disconnected -- on the bench all four corners flickered online/offline
/// constantly. 5s tolerates three consecutive misses while still counting as
/// alive.
///
/// This number matches the assumption already recorded in slave-watch.h ("a fast
/// heartbeat with a wide threshold"); the earlier 15s value was the point where
/// the code diverged from that design.
#ifndef ROOM_PUBLISH_MS
#define ROOM_PUBLISH_MS 5000UL
#endif
static const unsigned long PUBLISH_PERIOD_MS = ROOM_PUBLISH_MS;

/// How often to print a summary line to serial. Much less often than the send
/// cadence: this board runs for months with nobody plugged in, and a dense log
/// only scrolls away the lines worth reading when you do come to debug it.
static const unsigned long LOG_PERIOD_MS = 60000UL;

static unsigned long lastSend = 0;
static unsigned long lastLog = 0;
static uint32_t      sentCount = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n== BreezeLink - ESP32-C3 - ROOM CORNER SENSOR (ESP-NOW) ==");
  Serial.printf("corner=%d - DHT22 @GPIO%d - fw=%s\n", ROOM_CORNER, DHT_PIN, FW_VERSION);
  Serial.printf("uuid=%s\n", DEVICE_UUID);

  RoomSensor::begin(DHT_PIN);

  if (!EspNowSlaveRadio::begin(WIFI_SSID)) {
    Serial.println("esp_now_init FAILED - restarting");
    delay(2000);
    ESP.restart();
  }
}

void loop() {
  RoomSensor::poll();
  EspNowSlaveRadio::tickRescan(WIFI_SSID);

  const unsigned long now = millis();

  if (lastSend == 0 || now - lastSend >= PUBLISH_PERIOD_MS) {
    lastSend = now;

    float t = NAN, h = NAN;
    // read() returns false once the sensor has failed enough CONSECUTIVE times.
    // We still send the packet (with t/h left as NaN) rather than skipping it: a
    // packet with no reading is STILL a heartbeat, and it lets the gateway
    // distinguish "this corner is dead" from "this corner is alive but its sensor
    // is broken". Conflating those two sends the technician to check the wrong
    // thing.
    const bool ok = RoomSensor::read(t, h);

    AcEspNowPacket pkt;
    acEspNowFill(&pkt, DEVICE_UUID, t, h, AC_NODE_ROOM, (uint8_t)ROOM_CORNER);
    const bool sent = EspNowSlaveRadio::broadcast(&pkt, sizeof(pkt));
    if (sent) sentCount++;

    if (!ok) {
      Serial.printf("[room] no reading yet (%u consecutive failures) - check the DHT22 wiring "
                    "on GPIO%d and the 4.7k pull-up to 3.3V\n",
                    RoomSensor::consecutiveFailures(), DHT_PIN);
    }
  }

#if defined(ESPNOW_SNIFF) && ESPNOW_SNIFF
  // Report SNIFF results every 5s, decoupled from the 60s log cadence: while
  // chasing a radio problem, waiting a minute per attempt exhausts your patience
  // before it exhausts your hypotheses.
  {
    static unsigned long lastSniffLog = 0;
    static uint32_t      lastSniffCount = 0;
    if (now - lastSniffLog >= 5000UL) {
      lastSniffLog = now;
      const uint32_t c = EspNowSlaveRadio::sniffed();
      Serial.printf("  <sniff> %lu packets total (+%lu since 5s ago) - latest [%s] - real channel=%d\n",
                    (unsigned long)c, (unsigned long)(c - lastSniffCount),
                    EspNowSlaveRadio::sniffedLast(), EspNowSlaveRadio::channel());
      lastSniffCount = c;
    }
  }
#endif

  if (now - lastLog >= LOG_PERIOD_MS) {
    lastLog = now;
    float t = NAN, h = NAN;
    RoomSensor::read(t, h);
    // "sent" does NOT mean the gateway received it: broadcast has no ACK. To be
    // sure, check the gateway log or check whether the web UI shows this node's
    // numbers.
    Serial.printf("[room corner %d] t=%.1f h=%.0f - sent %lu packets - channel=%d - up=%lus",
                  ROOM_CORNER, t, h, (unsigned long)sentCount,
                  EspNowSlaveRadio::channel(), now / 1000UL);
#if defined(ESPNOW_SNIFF) && ESPNOW_SNIFF
    // Only present when built with -D ESPNOW_SNIFF=1 (see espnow-slave-radio.h).
    Serial.printf(" - HEARD %lu packets from other nodes [%s]",
                  (unsigned long)EspNowSlaveRadio::sniffed(),
                  EspNowSlaveRadio::sniffedLast());
#endif
    Serial.println();
  }

  delay(10);   // yield the CPU -- the ESP32-C3 has only ONE core
}
