/*
  BreezeLink - the bridge between the ESP32-S3 gateway and the UNO Q's Linux half.

  THIS IS THE PRODUCTION VERSION, differing from the test sketch in one respect: it
  does not merely print to the screen, it PUSHES DATA to Python. The control algorithm
  and the AI run over there.

    ESP32-S3 --UART D0/D1--> this sketch --RPC--> arduino-router --> Python
                            (frame check)                          (decisions)

  WIRING (cross-checked against ABX00162-full-pinout.pdf):
     ESP32-S3 GPIO18 (TX) --> D0 = PB7 = USART1_RX
     ESP32-S3 GPIO17 (RX) <-- D1 = PB6 = USART1_TX
     GND --- GND

  DO NOT CONNECT TO THE PINS LABELLED "RX"/"TX" on the other header: those are
  SOC_SE4_RX/TX, they go straight into the Qualcomm SoC and run at 1.8V. Feeding 3.3V
  into them destroys an SoC pin - and because the numbered header has NO RX/TX markings
  at all, this is a very easy mistake to make.

  ---------------------------------------------------------------------------
  THIS SKETCH DELIBERATELY DOES NOT UNDERSTAND THE PACKET. It validates the framing
  (magic + version + CRC) and forwards all 39 bytes to Python as hex. The packet layout
  therefore lives in only two places already pinned to each other by a static_assert -
  the C struct and protocol.py - rather than growing a third place here that has to be
  remembered.

  A SIDE BENEFIT: the sketch does not need to know ORG_ID. The link_key hashed from it
  is computed on the Python side, so this app directory can go anywhere without carrying
  any household's identity.

  IT STILL CHECKS THE CRC even though Python does too: a corrupt frame stopped here
  costs 0 bytes of RPC, whereas letting it through consumes a router round trip before
  being discarded. More importantly, the `g_bad` counter just below distinguishes "a
  noisy wire" from "dead RPC", and those are two faults needing completely different
  fixes.
*/

#include <Arduino_RouterBridge.h>

#include "led-matrix-logo.h"
#include "unoq-link-protocol.h"

// Serial1 = USART1 = D0/D1. Do NOT use Serial: the router has already taken that port
// to talk to the Linux half, so the index shifts by one - Serial1 is the numbered
// header.
#define GW_SERIAL Serial1
#define GW_BAUD   115200

// The RPC method names. They MUST MATCH edge_ai/bridge_client.py. A wrong name makes
// the router route into the void and REPORT NOTHING - notify is fire-and-forget, the
// other side simply never gets called, exactly like a broken cable.
static const char *RPC_SNAPSHOT = "gw/snapshot";
static const char *RPC_COMMAND  = "gw/command";

static uint8_t  g_buf[sizeof(AcUnoQSnapshot)];
static uint8_t  g_len = 0;

static uint32_t g_ok = 0;        // valid frames forwarded to Python
static uint32_t g_bad = 0;       // frames with a bad magic/version/CRC
static uint32_t g_bytes = 0;     // total raw bytes read from the wire
static uint32_t g_cmd = 0;       // commands from Python, written out to the wire
static uint32_t g_cmdBad = 0;    // commands from Python that were malformed
static uint32_t g_lastOkMs = 0;

// Do NOT name it HEX: Arduino already has `#define HEX 16` for Print, so a clashing
// declaration expands to `static const char 16[]` and produces a compile error on a
// line with nothing to do with the real mistake.
static const char HEXDIG[] = "0123456789abcdef";

/// Forward a validated frame to Python.
static void pushSnapshot() {
  // +1 for the terminator. A STATIC buffer rather than accumulating a String: this
  // function runs every 5 seconds for months, and concatenating 78 times per round is
  // 78 reallocations on a microcontroller with nobody to defragment for it.
  static char hex[sizeof(g_buf) * 2 + 1];
  for (uint8_t i = 0; i < sizeof(g_buf); i++) {
    hex[i * 2]     = HEXDIG[g_buf[i] >> 4];
    hex[i * 2 + 1] = HEXDIG[g_buf[i] & 0x0F];
  }
  hex[sizeof(g_buf) * 2] = '\0';

  Bridge.notify(RPC_SNAPSHOT, String(hex));
}

static void reportFrame(const AcUnoQSnapshot &s) {
  Monitor.print("[rx] rooms=");
  Monitor.print(s.room_count);

  Monitor.print("  t_in=");
  if (s.t_in_c100 == AC_UNOQ_T_INVALID) Monitor.print("--");
  else Monitor.print(s.t_in_c100 / 100.0f, 1);

  Monitor.print("  t_out=");
  if (s.t_out_c100 == AC_UNOQ_T_INVALID) Monitor.print("--");
  else Monitor.print(s.t_out_c100 / 100.0f, 1);

  Monitor.print("  flags=0x");
  Monitor.print(s.flags, HEX);
  Monitor.print("  silence=");
  if (s.cloud_silence_sec == AC_UNOQ_SILENCE_NEVER) Monitor.print("never");
  else { Monitor.print(s.cloud_silence_sec); Monitor.print("s"); }
  Monitor.print("  #");
  Monitor.println(g_ok);
}

/// Decode a frame once enough bytes are present. Returns false if invalid.
static bool decodeFrame() {
  AcUnoQSnapshot s;
  memcpy(&s, g_buf, sizeof(s));

  if (s.magic != AC_UNOQ_MAGIC) return false;
  if (s.version != AC_UNOQ_VERSION) {
    Monitor.print("[!] the gateway sent version ");
    Monitor.print(s.version);
    Monitor.print(", this sketch understands ");
    Monitor.print(AC_UNOQ_VERSION);
    Monitor.println(" - reflash one of them");
    return false;
  }
  if (!acUnoQCheckSnapshot(&s)) return false;

  g_ok++;
  g_lastOkMs = millis();
  pushSnapshot();
  reportFrame(s);
  return true;
}

static void pump() {
  while (GW_SERIAL.available() > 0) {
    const uint8_t b = (uint8_t)GW_SERIAL.read();
    g_bytes++;

    if (g_len == 0 && b != AC_UNOQ_MAGIC) continue;   // wait for the magic byte
    g_buf[g_len++] = b;
    if (g_len < sizeof(g_buf)) continue;

    if (decodeFrame()) {
      g_len = 0;
    } else {
      g_bad++;
      // Slide ONE byte and keep looking for the magic, do NOT clear the buffer: the
      // real magic byte may be inside the bytes just collected (half an old frame stuck
      // to half a new one), and clearing would also discard a good frame sitting
      // immediately after a corrupt one.
      memmove(g_buf, g_buf + 1, sizeof(g_buf) - 1);
      g_len = sizeof(g_buf) - 1;
      while (g_len > 0 && g_buf[0] != AC_UNOQ_MAGIC) {
        memmove(g_buf, g_buf + 1, g_len - 1);
        g_len--;
      }
    }
  }
}

static int8_t nibble(char c) {
  if (c >= '0' && c <= '9') return (int8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
  return -1;
}

/// Python calls this to send an advice/command down to the gateway.
///
/// IT RUNS ON THE BRIDGE'S THREAD, and that thread has only 500 bytes of stack - so
/// everything here has to be small and non-recursive. 13 bytes on the stack is fine.
static void onCommand(String hex) {
  const size_t need = sizeof(AcUnoQCommandHeader) * 2;
  uint8_t out[sizeof(AcUnoQCommandHeader)];

  if (hex.length() != need) {
    g_cmdBad++;
    return;
  }
  for (size_t i = 0; i < sizeof(out); i++) {
    const int8_t hi = nibble(hex[i * 2]);
    const int8_t lo = nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      g_cmdBad++;
      return;
    }
    out[i] = (uint8_t)((hi << 4) | lo);
  }

  // Validate BEFORE writing to the wire. The gateway checks too and will refuse a
  // corrupt packet, but a corrupt command that gets that far only shows up in the
  // gateway's log - which nobody is watching while debugging this side.
  AcUnoQCommandHeader c;
  memcpy(&c, out, sizeof(c));
  if (c.magic != AC_UNOQ_MAGIC || c.version != AC_UNOQ_VERSION || !acUnoQCheckCommand(&c)) {
    g_cmdBad++;
    return;
  }

  GW_SERIAL.write(out, sizeof(out));
  g_cmd++;

  Monitor.print("[tx] ");
  Monitor.print(c.kind == AC_UNOQ_KIND_COMMAND ? "COMMAND" : "advice");
  // Cast to int: `setpoint` is an int8_t, and Print has its own overload for `char` -
  // left as is, 25 prints as a control character rather than "25".
  Monitor.print(" mode=");
  Monitor.print((int)c.mode);
  Monitor.print(" set=");
  Monitor.print((int)c.setpoint);
  Monitor.print(" seq=");
  Monitor.println(c.seq);
}

void setup() {
  Monitor.begin(115200);
  // Bridge has to come up FIRST: without it Monitor has no route back to Python and
  // every log line below falls into the void.
  Bridge.begin();
  while (!Monitor) {
    delay(200);
  }

  GW_SERIAL.begin(GW_BAUD);
  Bridge.provide(RPC_COMMAND, onCommand);
  LedLogo::begin();

  Monitor.println("== BreezeLink - UART <-> Python bridge ==");
  Monitor.print("waiting for ");
  Monitor.print((unsigned)sizeof(AcUnoQSnapshot));
  Monitor.println("-byte frames on D0/D1 @115200");
}

void loop() {
  // READ THE UART FIRST. Zephyr's UART buffer is finite, and a 39-byte frame arriving
  // while we are busy rendering the LED matrix is a lost frame.
  pump();
  LedLogo::update();

  // A summary every 30 seconds - sparser than the test sketch, because in production
  // the Python log is what needs reading, and a summary line every 5 seconds would
  // scroll it away.
  static uint32_t lastReport = 0;
  if (millis() - lastReport >= 30000) {
    lastReport = millis();
    Monitor.print("[total] bytes=");
    Monitor.print(g_bytes);
    Monitor.print("  frames=");
    Monitor.print(g_ok);
    Monitor.print("  bad=");
    Monitor.print(g_bad);
    Monitor.print("  commands=");
    Monitor.print(g_cmd);
    if (g_cmdBad) {
      Monitor.print("(+");
      Monitor.print(g_cmdBad);
      Monitor.print(" bad)");
    }
    // Distinguish three completely different failures, because the fixes differ
    // completely.
    if (g_bytes == 0) {
      Monitor.println("  <- NO BYTES AT ALL: check the S3 GPIO18 -> D0 wire, a common GND, and whether the S3 is running");
    } else if (g_ok == 0) {
      Monitor.println("  <- BYTES BUT NO FRAMES: almost certainly a baud mismatch or a missing GND");
    } else {
      Monitor.print("  last ");
      Monitor.print((millis() - g_lastOkMs) / 1000);
      Monitor.println("s ago");
    }
  }

  delay(5);
}
