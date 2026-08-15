#pragma once
#include <Arduino.h>

// ============================================================================
//  Keep the panel on THE SAME CHANNEL the nodes are transmitting on -- even when
//  WiFi is down.
// ----------------------------------------------------------------------------
//  WHY IT IS NEEDED. ESP-NOW requires both sides on the same channel. The
//  room-corner nodes and the outdoor node scan for the router's SSID and lock onto
//  its channel (shared/espnow-slave-radio.h), so THE ROUTER IS THE WHOLE SYSTEM'S
//  MEETING POINT. The panel, meanwhile, used to have no line of code selecting a
//  channel at all: it simply inherited whatever channel the station interface was
//  on.
//
//  The consequence: losing WiFi meant losing the meeting point. Worse, the
//  reconnect loop called a bare `WiFi.begin()` every 15 seconds, and with no
//  channel hint `begin()` makes the station SCAN ACROSS EVERY CHANNEL looking for
//  the SSID -- so the panel's radio never settles, and any node packet arriving
//  while it is on another channel is lost. Broadcast has no ACK, so the node still
//  reports a successful send and not one error line appears anywhere.
//
//  THE APPROACH -- MIRROR THE NODES' BEHAVIOUR, DO NOT INVENT ANYTHING NEW.
//  The nodes cannot be changed (4 boards on the wall + 1 outdoors, WITH NO OTA),
//  so the panel has to be the side that adapts. The nodes' actual behaviour, read
//  from espnow-slave-radio.h:
//
//      at boot     : scan 3 times -> found: lock to the router's channel
//                                 -> missed: lock to channel 1
//      every 5 min : scan         -> found: lock to the router's channel
//                                 -> missed: KEEP the current channel
//
//  Note that last line: a node NEVER falls back to channel 1 when the router dies
//  mid-run, it stubbornly holds the last channel it knew. So the panel must also
//  remember its last channel and return to exactly that -- and NOT fall back to
//  channel 1. This is the easiest thing to get wrong: "both sides fall back to
//  channel 1" sounds reasonable but would push the panel to channel 1 while the
//  nodes stay on the old one, manufacturing the very channel mismatch this is
//  meant to cure.
//
//  THE CHANNEL HAS TO SURVIVE A RESTART, so it lives in NVS: a whole-house power
//  cut followed by power returning while the router is broken is a real scenario,
//  and a freshly booted panel has nothing in RAM to remember.
// ============================================================================
namespace EspNowChannel {

/// The channel to use when the router has NEVER been seen (freshly flashed board,
/// empty NVS).
///
/// IT MUST BE 1, and must not be changed to a "nicer" number: this is exactly the
/// channel EspNowSlaveRadio::begin() falls back to when its 3 boot scans all miss.
/// If both sides get lost, they still have to get lost in the same place in order
/// to find each other.
static const uint8_t FALLBACK_CHANNEL = 1;

/// How often to re-scan for the router's channel while WiFi is down (ms).
///
/// EXACTLY EQUAL TO the nodes' EspNowSlaveRadio::RESCAN_INTERVAL_MS. That is not a
/// coincidence and should not be "optimised" away from it: scanning less often
/// than the nodes leaves a window where the panel is still on the old channel
/// after the nodes have moved; scanning more often costs extra scans, each of
/// which pulls the radio away from receiving for ~1 second.
static const uint32_t RESCAN_INTERVAL_MS = 300000UL;

/// Load the remembered channel from NVS. Call in setup(), BEFORE connectWifi().
void begin();

/// Record the router channel just observed (call every time WiFi connects
/// successfully).
/// Only touches NVS when the number actually changes -- flash has a finite write
/// endurance.
void note(uint8_t channel);

/// The remembered channel, or FALLBACK_CHANNEL if the router has never been seen.
uint8_t last();

/// Pin the radio to the remembered channel. Returns false if the hardware refuses.
///
/// ONLY CALL THIS ONCE WiFi IS DOWN, and it MUST be called after
/// `WiFi.disconnect(false)`: while the station is still probing for networks, the
/// WiFi stack drives the channel as it pleases and the channel set here is
/// silently overwritten. Calling it while actually connected to the router is
/// worse still -- it cuts the WiFi link with nobody having asked for that.
bool park();

/// Stop pinning (WiFi is back -- from now on the router holds the channel for us).
void release();

/// Is the radio still on the pinned channel; pull it back if it has drifted. Call
/// every loop() WHILE PARKED (do not call in the middle of a connection attempt,
/// see below).
///
/// CHEAP: it only reads the channel register, and only writes when there really is
/// a mismatch -- so calling it every loop costs nothing.
///
/// WHY IT IS NEEDED even though park() already reads back to confirm: park() is
/// only correct AT THE MOMENT it is called. The WiFi stack can drive the channel
/// later for reasons we did not initiate (an internal event, a leftover probe).
/// Without this function, the earliest fix would be the next retry -- i.e. up to 5
/// minutes of silence with nothing reporting it.
///
/// DO NOT CALL IT DURING A CONNECTION ATTEMPT: at that point the WiFi stack is
/// deliberately hopping channels to find the SSID, and pulling it back makes the
/// two fight each other so no connection ever completes.
void hold();

/// Whether the panel is currently pinning the channel itself. Used by the INFO
/// screen: an installer needs to distinguish "the router decides this channel"
/// from "the panel remembered this channel" -- those two states lead to entirely
/// different places to go looking.
bool pinned();

/// Scan for [ssid] to refresh the channel, then RE-LOCK. Returns true if the
/// router was found.
///
/// ALWAYS RE-LOCK EVEN WHEN THE CHANNEL HAS NOT CHANGED, and even when the scan
/// missed. scanNetworks() hops across every channel and leaves the radio on
/// whichever one it stopped on -- it does NOT restore the previous one. This is
/// exactly the trap that once made a node "die silently", recorded in
/// EspNowSlaveRadio::tickRescan -- do not let the panel step in it again.
///
/// IT COSTS ~1 SECOND during which no packets are received, so call it rarely (see
/// RESCAN_INTERVAL_MS) and never while WiFi is connected normally.
bool rescan(const char *ssid);

} // namespace EspNowChannel
