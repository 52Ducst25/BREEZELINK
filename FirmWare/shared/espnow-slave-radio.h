#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>        // esp_wifi_set_channel()
#include <esp_idf_version.h>

// ============================================================================
//  ESP-NOW radio for a SLAVE node (does not join WiFi, only locks onto the
//  router's channel).
// ----------------------------------------------------------------------------
//  Shared between the OUTDOOR node and the 4 ROOM-CORNER nodes. This logic used
//  to live inside the outdoor node's main; it was extracted because the four room
//  nodes need exactly the same thing, and with two copies of an already-costly
//  trap the next fix would certainly miss one of them.
//
//  ESP-NOW'S BIGGEST TRAP IS A CHANNEL MISMATCH: both sides must be on the same
//  WiFi channel, and the gateway follows the router's channel. So this node does
//  NOT hard-code a channel -- it SCANS for the household SSID to learn which
//  channel the router is on and locks onto that. If the router changes channel,
//  the next scan picks it up.
//
//  This node does NOT join WiFi and does NOT need the WiFi password -- it only
//  *scans* to read the channel number. That is why its config.h holds no secrets.
//
//  It sends a BROADCAST rather than unicasting to the gateway's MAC: installation
//  does not have to fetch the gateway MAC and program it into each board -- power
//  it on and it works. A household has only one gateway, so there is no risk of
//  addressing the wrong one.
// ============================================================================
namespace EspNowSlaveRadio {

/// How often to re-scan for the router's channel (ms). 5 minutes: quick enough to
/// keep up with a router changing channel, sparse enough that the scan (which
/// keeps the radio busy for ~1s) does not disturb the send cadence.
static const unsigned long RESCAN_INTERVAL_MS = 300000UL;

namespace detail {

inline uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
inline int     currentChannel = 0;
inline bool    lastSendOk = false;
inline unsigned long lastRescanMs = 0;

// The SEND callback signature changed in Arduino-ESP32 core 3.2 (IDF 5.4): the
// first parameter went from `const uint8_t *mac` to `const wifi_tx_info_t *`. It
// has to be detected via ESP_IDF_VERSION and NOT via
// ESP_ARDUINO_VERSION_MAJOR >= 3 like the gateway's RECEIVE callback: the two
// callbacks changed at two different version boundaries (receive changed in core
// 3.0, send not until core 3.2).
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
inline void onSent(const wifi_tx_info_t *, esp_now_send_status_t status) {
  lastSendOk = (status == ESP_NOW_SEND_SUCCESS);
}
#else
inline void onSent(const uint8_t *, esp_now_send_status_t status) {
  lastSendOk = (status == ESP_NOW_SEND_SUCCESS);
}
#endif

// --- SNIFF mode (debugging only) -------------------------------------------
// Enable with `-D ESPNOW_SNIFF=1`. OFF BY DEFAULT: a room-corner node only has to
// transmit, and a receive callback running all day only burns CPU on something
// nobody reads.
//
// WHY IT IS WORTH HAVING: when the gateway reports "0 packets received" there are
// two completely different possibilities -- the node is not actually radiating,
// or the node radiates and the gateway cannot hear it. ESP-NOW's SEND callback
// cannot tell them apart: for broadcast it always reports SUCCESS because there
// is no ACK. Turn this flag on for ONE node and it can hear its three siblings
// (same channel, all equally not joined to WiFi) and the question splits in two
// immediately.
#if defined(ESPNOW_SNIFF) && ESPNOW_SNIFF
inline volatile uint32_t sniffCount = 0;
inline char sniffLast[40] = "";

inline void onSniff(const uint8_t *mac, const uint8_t *data, int len) {
  sniffCount++;
  if (mac) {
    snprintf(sniffLast, sizeof(sniffLast), "%02X:%02X:%02X:%02X:%02X:%02X/%dB",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], len);
    // DO NOT print here. The callback runs in the WiFi task, and Serial on this
    // board is USB CDC -- writing from another task can block, and a measurement
    // that corrupts itself is worse than no measurement. loop() prints instead
    // (see esp32-room/src/main.cpp).
    (void)data;
  }
}
#endif

}  // namespace detail

/// Scan the nearby networks to learn which channel the household router is on.
/// Returns 0 if the SSID is not found (router off / out of range).
inline int findRouterChannel(const char *ssid) {
  const int n = WiFi.scanNetworks(false /*async*/, true /*show hidden*/);
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == ssid) {
      const int ch = WiFi.channel(i);
      WiFi.scanDelete();
      return ch;
    }
  }
  WiFi.scanDelete();
  return 0;
}

/// Lock onto channel [ch] and re-register the broadcast peer on that channel.
inline bool bindToChannel(int ch) {
  if (ch <= 0) return false;

  // WIFI_SECOND_CHAN_NONE: 20MHz channel only. ESP-NOW does not need a secondary
  // channel, and declaring one that differs from the gateway's breaks things.
  //
  // CHECK THE RETURN VALUE. An earlier version called this and ignored it, so a
  // single failed channel change left the node silently transmitting on a
  // different channel while every log line still reported the intended one.
  const esp_err_t chErr = esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);
  if (chErr != ESP_OK) {
    Serial.printf("esp_wifi_set_channel(%d) FAILED: %s - node will transmit on the wrong channel\n",
                  ch, esp_err_to_name(chErr));
  }

  // Remove the old peer if it was on a different channel. If the peer does not
  // exist the call returns ESP_ERR_ESPNOW_NOT_FOUND -- harmless, ignore it.
  esp_now_del_peer(detail::BROADCAST_MAC);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, detail::BROADCAST_MAC, 6);
  // channel = 0 means "send on the interface's current channel", NOT "any
  // channel". The radio was just put on the required channel by
  // esp_wifi_set_channel() above, so 0 and ch refer to the same channel -- but
  // only 0 actually works.
  //
  // MEASURED, DO NOT CHANGE THIS BACK: with channel = ch the node reports
  // ESP_NOW_SEND_SUCCESS for every packet while NO device can hear it -- not the
  // gateway, not even an identical board sitting 20cm away. Changing just this
  // one number makes the packets arrive immediately. The reason: when a peer
  // declares a channel other than 0, ESP-NOW hops channels around every send; on
  // a station interface NOT joined to WiFi the frame goes out mid-hop and is lost
  // -- while the send callback still reports success, because broadcast has no
  // ACK.
  //
  // How to spot it: have the gateway transmit back (it declares channel = 0) and
  // the node hears it perfectly. Good reception with mute transmission is the
  // signature of this bug.
  peer.channel = 0;
  peer.encrypt = false;
  peer.ifidx   = WIFI_IF_STA;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Could not add the ESP-NOW peer");
    return false;
  }
  detail::currentChannel = ch;
  // Print BOTH the intended channel and the REAL one: a discrepancy between the
  // two numbers means you have already found the bug.
  uint8_t primary = 0;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&primary, &second);
  Serial.printf("ESP-NOW locked to channel %d - radio is actually on channel %u (secondary=%d)\n",
                ch, primary, (int)second);
  return true;
}

/// Bring the radio up as a station-without-joining and lock onto [ssid]'s
/// channel. Call this from setup(). Returns false if esp_now_init() fails (the
/// caller decides whether to restart or carry on).
inline bool begin(const char *ssid) {
  // STA but NOT connected: ESP-NOW needs the station interface up, not membership
  // of the network.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Disable modem power saving. By default the ESP32 lets the radio sleep between
  // beacons, and while asleep an outgoing ESP-NOW frame is delayed or dropped --
  // exactly the "silent death" bug already seen on the gateway (git log: "master
  // goes deaf to ESP-NOW after a few minutes").
  WiFi.setSleep(false);

  // FORCE THE PHY TO B/G/N EXPLICITLY, DO NOT TRUST THE DEFAULT.
  //
  // Measured on an ESP32-C3 board: the node reported `ESP_NOW_SEND_SUCCESS` for
  // every packet, the radio was on the correct channel 1, and NO device could
  // hear it -- not even another board 20cm away. Meanwhile the reverse direction
  // (gateway transmitting, this node listening) barely dropped a packet. Good
  // reception with transmissions nobody understands is the signature of a PHY
  // problem: if the interface has ended up in LR mode (long-range, an Espressif
  // extension) then only LR devices can decode what it sends, while receiving
  // standard frames still works normally -- exactly the symptom.
  //
  // Declaring it explicitly removes any dependence on what the core/board default
  // happens to be.
  const esp_err_t protoErr =
      esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  if (protoErr != ESP_OK) {
    Serial.printf("esp_wifi_set_protocol FAILED: %s\n", esp_err_to_name(protoErr));
  }

  // TRANSMIT POWER 8dBm, NOT MAXIMUM -- AND THIS IS WHAT ONCE MUTED THE WHOLE
  // SYSTEM.
  //
  // The unit is 0.25dBm, so 32 = 8dBm and 78 = 19.5dBm (the maximum, and the
  // chip's default). At maximum, a USB-powered ESP32-C3 board transmits something
  // NO DEVICE can decode: the gateway receives 0 packets, another C3 board 20cm
  // away also receives 0 packets, while the reverse direction (gateway
  // transmitting, node listening) is perfectly clean. Dropping to 8dBm makes the
  // packets arrive immediately.
  //
  // Why: the current peak while transmitting at maximum is around 350mA. A weak
  // USB port or hub cannot supply it, the voltage sags mid-transmission and the
  // power amplifier emits garbage. THERE IS NOT ONE ERROR LINE anywhere: the send
  // callback still reports ESP_NOW_SEND_SUCCESS, because broadcast has no ACK so
  // it only knows the frame left the MAC layer, not what actually went out over
  // the air.
  //
  // 8dBm still covers a room with room to spare (ESP-NOW at this level reaches
  // tens of metres indoors), so this is not a sacrifice -- it just drops the part
  // of the power budget the supply cannot deliver anyway.
  //
  // Raise it again with `-D SLAVE_TX_POWER=n` IF the node has a decent supply (its
  // own adapter, adequate decoupling) and needs more range. After raising it you
  // MUST re-verify with the gateway's `[relay]` log; do not trust the node's own
  // "send succeeded".
#ifndef SLAVE_TX_POWER
#define SLAVE_TX_POWER 32
#endif
  esp_wifi_set_max_tx_power(SLAVE_TX_POWER);
  int8_t txPower = 0;
  esp_wifi_get_max_tx_power(&txPower);

  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_send_cb(detail::onSent);
  Serial.printf("ESP-NOW: transmit power %.1f dBm\n", txPower * 0.25f);
#if defined(ESPNOW_SNIFF) && ESPNOW_SNIFF
  esp_now_register_recv_cb(detail::onSniff);
  Serial.println("ESP-NOW: SNIFF mode on - will count other nodes' packets");
#endif

  // Scan a few times: the very first scan after bringing the radio up often
  // misses, and locking onto the wrong channel sends the packets into the void
  // with NO way of knowing (broadcast has no ACK).
  // DEBUGGING shortcut: `-D SLAVE_FIXED_CHANNEL=n` skips scanning entirely and
  // locks straight onto channel n. Scanning is the one thing this node does that
  // the gateway does not, so when you suspect the scan itself is breaking
  // transmission, this is how you rule it out.
  // NOT for production use: if the router changes channel the node goes mute and
  // nobody finds out.
#if defined(SLAVE_FIXED_CHANNEL) && SLAVE_FIXED_CHANNEL > 0
  Serial.printf("SKIPPING SCAN (SLAVE_FIXED_CHANNEL=%d) - debugging use only\n",
                SLAVE_FIXED_CHANNEL);
  bindToChannel(SLAVE_FIXED_CHANNEL);
  detail::lastRescanMs = millis();
  return true;
#endif

  int ch = 0;
  for (uint8_t attempt = 1; attempt <= 3 && ch == 0; attempt++) {
    delay(300);
    ch = findRouterChannel(ssid);
    if (ch == 0) Serial.printf("Scan %u: \"%s\" not found yet\n", attempt, ssid);
  }
  if (ch == 0) {
    Serial.printf("SSID \"%s\" not found - using channel 1 for now, rescanning every %lus\n",
                  ssid, RESCAN_INTERVAL_MS / 1000);
    ch = 1;
  }
  bindToChannel(ch);
  detail::lastRescanMs = millis();
  return true;
}

/// Call every loop(): re-scan for the channel PERIODICALLY.
///
/// Periodically rather than on a send failure: broadcast packets have no ACK, so
/// the callback always reports success even when transmitting on the wrong
/// channel, and nobody would ever notice.
///
/// AND THE CHANNEL MUST BE RE-BOUND AFTER EVERY SCAN, even when it has not
/// changed: scanNetworks() hops across every channel and leaves the radio on
/// whichever one it stopped on -- it does NOT restore the previous channel. An
/// earlier version only re-bound when the channel changed, so after every scan the
/// radio was on the wrong channel and every outgoing packet fell into the void --
/// and since broadcast has no ACK nothing reported an error, the node just "died
/// silently".
inline void tickRescan(const char *ssid) {
  const unsigned long now = millis();
  if (now - detail::lastRescanMs < RESCAN_INTERVAL_MS) return;
  detail::lastRescanMs = now;

  int ch = findRouterChannel(ssid);
  if (ch <= 0) ch = detail::currentChannel;   // scan missed -> keep the current channel
  bindToChannel(ch);
}

/// Broadcast one packet and wait for the callback to report the result.
///
/// NOTE: for a broadcast, true only means THE RADIO TRANSMITTED IT -- there is no
/// ACK, so there is no way to know whether the gateway received it. To be sure,
/// check the gateway log, or check whether the web/app shows this node's numbers.
inline bool broadcast(const void *payload, size_t len) {
  detail::lastSendOk = false;
  esp_now_send(detail::BROADCAST_MAC, (const uint8_t *)payload, len);
  delay(60);   // wait for the callback
  return detail::lastSendOk;
}

/// The radio's REAL channel, read back from the hardware.
///
/// THIS FUNCTION USED TO RETURN THE STORED VARIABLE, and that was a dangerous
/// lie: bindToChannel() ignored esp_wifi_set_channel()'s return value, so when
/// that call failed the variable still said "channel 1" while the radio sat
/// somewhere else. The log confidently printed "channel=1" on BOTH nodes while
/// they could not hear each other at all -- exactly the kind of thing that makes
/// a reader rule out the correct cause.
inline int channel() {
  uint8_t primary = 0;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary != 0) return primary;
  return detail::currentChannel;
}

/// The channel the firmware INTENDED to lock onto (to compare against channel()
/// above while debugging).
inline int intendedChannel() { return detail::currentChannel; }

/// How many ESP-NOW packets this node HEARD from other devices (0 unless
/// ESPNOW_SNIFF is enabled).
inline uint32_t sniffed() {
#if defined(ESPNOW_SNIFF) && ESPNOW_SNIFF
  return detail::sniffCount;
#else
  return 0;
#endif
}

/// Description of the most recently heard packet ("MAC/length"), empty if nothing
/// has been heard.
inline const char *sniffedLast() {
#if defined(ESPNOW_SNIFF) && ESPNOW_SNIFF
  return detail::sniffLast;
#else
  return "";
#endif
}

}  // namespace EspNowSlaveRadio
