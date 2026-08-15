#include "espnow-channel.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>

namespace EspNowChannel {

// ITS OWN NVS namespace, not sharing IrStore's "aircon-ir": the IR code store can
// fill up (the nvs partition is only 20 KB) and once it is full every write into
// that namespace fails. Losing the channel number along with the IR codes would
// leave the panel channel-blind just because the user learned one more remote --
// two things with nothing to do with each other.
static Preferences prefs;
static bool    ready    = false;
static uint8_t current  = FALLBACK_CHANNEL;
static bool    isPinned = false;

/// Is this channel number inside the valid 2.4 GHz range.
///
/// CHECKED ON EVERY ENTRY POINT. `WiFi.channel()` returns 0 when it does not know
/// yet, and writing a 0 into NVS makes the panel lock onto "channel 0" on the next
/// boot -- esp_wifi_set_channel() refuses, the function returns an error, and the
/// panel ends up somewhere nobody knows.
static bool valid(int ch) { return ch >= 1 && ch <= 14; }

void begin() {
  ready = prefs.begin("bl-radio", false /*read-write*/);
  if (!ready) {
    Serial.printf("[channel] could not open NVS - will use channel %u and CANNOT remember it\n",
                  FALLBACK_CHANNEL);
    return;
  }
  const int saved = prefs.getUChar("ch", 0);
  if (valid(saved)) {
    current = (uint8_t)saved;
    Serial.printf("[channel] remembered from last time: channel %u\n", current);
  } else {
    // The router has never been seen. Say plainly that this is a GUESS and not a
    // measurement -- if the nodes have locked onto a different channel, this is the
    // very line that explains the silence.
    Serial.printf("[channel] router never seen - using channel %u for now (like the nodes at boot)\n",
                  FALLBACK_CHANNEL);
  }
}

void note(uint8_t channel) {
  if (!valid(channel)) return;
  if (channel == current) return;

  const uint8_t before = current;
  current = channel;
  if (ready) prefs.putUChar("ch", current);
  Serial.printf("[channel] router is on channel %u (was %u) - remembered\n", current, before);
}

uint8_t last() { return current; }

bool park() {
  const esp_err_t err = esp_wifi_set_channel(current, WIFI_SECOND_CHAN_NONE);
  if (err != ESP_OK) {
    Serial.printf("[channel] COULD NOT pin channel %u: %s\n", current, esp_err_to_name(err));
    return false;
  }

  // READ BACK FROM THE HARDWARE, do not trust the command just issued. The same
  // reason recorded in EspNowSlaveRadio::channel(): an internal variable saying
  // "channel 6" while the radio sits elsewhere is a confident lie of a log line,
  // and it makes the reader rule out the one cause that is actually true.
  uint8_t primary = 0;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&primary, &second);

  const bool wasPinned = isPinned;
  isPinned = true;
  // Print ONE line per TRANSITION into the pinned state, not on every re-lock:
  // this function is called after every failed connection attempt, and an
  // overnight outage means hundreds of them -- enough to bury every other log
  // line.
  if (!wasPinned || primary != current) {
    Serial.printf("[channel] WiFi lost -> pinned to channel %u (radio really on channel %u) - "
                  "ESP-NOW still receiving normally\n", current, primary);
  }
  return primary == current;
}

void release() {
  if (!isPinned) return;
  isPinned = false;
  Serial.println("[channel] WiFi reconnected -> unpinned, the router holds the channel for us");
}

bool pinned() { return isPinned; }

void hold() {
  if (!isPinned) return;

  uint8_t primary = 0;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  if (esp_wifi_get_channel(&primary, &second) != ESP_OK) return;
  if (primary == current) return;

  // Print EVERY drift, with no rate limiting: this is not normal. If this line
  // repeats, something else is driving the radio, and knowing how many times a
  // minute it happens is exactly the clue for finding it.
  Serial.printf("[channel] radio drifted to channel %u - pulling back to %u\n", primary, current);
  esp_wifi_set_channel(current, WIFI_SECOND_CHAN_NONE);
}

bool rescan(const char *ssid) {
  if (ssid == nullptr || ssid[0] == '\0') return false;

  const int n = WiFi.scanNetworks(false /*async*/, true /*show hidden*/);
  int found = 0;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == ssid) { found = WiFi.channel(i); break; }
  }
  WiFi.scanDelete();

  if (valid(found)) {
    note((uint8_t)found);
  } else {
    Serial.printf("[channel] rescan: \"%s\" not found - keeping channel %u (like the nodes)\n",
                  ssid, current);
  }

  // RE-LOCK UNCONDITIONALLY -- even when the channel has not changed, even when the
  // scan missed. See this function's note in espnow-channel.h: after a scan the
  // radio sits on whichever channel it stopped on, not where we want it.
  //
  // Force one line to be printed again by clearing the flag first: after a scan,
  // which channel the panel returned to is news worth recording, not noise.
  isPinned = false;
  park();
  return valid(found);
}

} // namespace EspNowChannel
