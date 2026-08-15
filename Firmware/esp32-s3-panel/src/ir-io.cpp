#include "ir-io.h"
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <string.h>

namespace IrIo {

/// Capture buffer. An air conditioner frame is many times longer than a TV
/// remote's, so 1024 is the size the library recommends specifically for air
/// conditioners; the default (~100) would capture a truncated tail.
static const uint16_t CAPTURE_BUFFER = 1024;

/// How much silence is enough to conclude "end of frame" (ms). Air conditioner
/// remotes have longer gaps between bursts than ordinary remotes -- leaving it at
/// the 15ms default splits one button press into two frames and learns a truncated
/// code.
static const uint8_t CAPTURE_TIMEOUT_MS = 50;

/// The standard carrier for consumer remotes.
static const uint16_t CARRIER_KHZ = 38;

/// Below this length it is almost certainly noise (a fluorescent lamp, another
/// remote sweeping past) rather than a real button press. Even the shortest air
/// conditioner frame has several tens of transitions. Discard it and keep
/// learning, rather than uploading junk to the backend.
static const uint16_t MIN_RAW_LEN = 20;

static IRsend *sender = nullptr;
static IRrecv *receiver = nullptr;
static decode_results results;

static bool     active = false;
static bool     timedOut = false;
static uint32_t deadline = 0;

void begin(uint8_t txPin, uint8_t rxPin) {
  sender = new IRsend(txPin);
  sender->begin();
  // Allocate only, NO enableIRIn() yet -- the receiver is only enabled in learn
  // mode.
  receiver = new IRrecv(rxPin, CAPTURE_BUFFER, CAPTURE_TIMEOUT_MS, true);
}

void blast(const uint16_t *raw, uint16_t len) {
  if (sender == nullptr || raw == nullptr || len == 0) return;

  // If a transmit request arrives mid-learn: disable the receiver while
  // transmitting, otherwise the node captures its own frame and mistakes it for
  // the user pressing the remote.
  bool wasLearning = active;
  if (wasLearning && receiver) receiver->disableIRIn();

  sender->sendRaw(raw, len, CARRIER_KHZ);

  if (wasLearning && receiver) {
    receiver->enableIRIn();
    receiver->resume();
  }
}

void learnStart(uint32_t timeoutMs) {
  if (receiver == nullptr) return;

  // DISABLE FIRST IF A LEARN IS STILL IN PROGRESS. IRremoteESP8266's enableIRIn()
  // calls timerBegin() + timerAttachInterrupt() + attachInterrupt() EVERY TIME,
  // without checking whether they are already attached. Calling it on top of
  // itself makes ESP-IDF refuse:
  //     addApbChangeCallback(): duplicate func=...
  //     timer_group: timer_isr_callback_add(236): register interrupt service failed
  // and the sampling timer DOES NOT run -> the receiver is dead until a restart,
  // while the log cheerfully keeps printing "[learn] point the remote at the
  // receiver and press the button". A silent failure of the worst kind: the
  // installer keeps pressing the remote to no effect and goes off suspecting the
  // receiver, the wiring, or the distance.
  //
  // Re-entering learn mode mid-learn is NOT rare: the backend resends the command,
  // the user presses "learn" again because they were not ready the first time, or
  // they switch to learning a different button. disableIRIn() cleans up fully
  // (timerEnd + detachInterrupt) so calling it again is safe.
  if (active) receiver->disableIRIn();

  receiver->enableIRIn();
  receiver->resume();
  active   = true;
  timedOut = false;
  deadline = millis() + timeoutMs;
}

bool learning() { return active; }

uint32_t learnRemainingMs() {
  if (!active) return 0;
  // Signed arithmetic so it stays correct across a millis() wrap (~49 days) -- the
  // same reason as expired() below.
  const int32_t left = (int32_t)(deadline - millis());
  return left > 0 ? (uint32_t)left : 0;
}

void learnStop() {
  if (receiver) receiver->disableIRIn();
  active = false;
}

bool learnTimedOut() {
  bool t = timedOut;
  timedOut = false;   // report once and no more, rather than every loop()
  return t;
}

/// Has the wait expired? Signed arithmetic so it stays correct across a millis()
/// wrap (~49 days).
static bool expired() { return (int32_t)(millis() - deadline) >= 0; }

uint16_t learnPoll(uint16_t *out, uint16_t maxLen) {
  if (!active || receiver == nullptr || out == nullptr) return 0;

  if (!receiver->decode(&results)) {
    if (expired()) {
      timedOut = true;
      learnStop();
    }
    return 0;
  }

  // resultToRawArray() returns a microsecond array already compensated for
  // kMarkExcess and with the leading silence REMOVED -- exactly the alternating
  // mark/space layout the backend stores in ir_codes.raw_timing and sends back in
  // ir_raw. The array is allocated with new[], so it must be delete[]d.
  uint16_t len = getCorrectedRawLength(&results);
  uint16_t *raw = resultToRawArray(&results);
  uint16_t taken = 0;

  if (raw == nullptr) {
    Serial.println("[ir] out of RAM while reading the frame - press the remote again");
  } else if (len < MIN_RAW_LEN) {
    Serial.printf("[ir] ignoring noise (%u transitions) - still waiting for you to press the remote\n", len);
  } else if (len > maxLen) {
    Serial.printf("[ir] frame of %u transitions exceeds the %u limit - discarded (a truncated frame is a wrong code)\n",
                  len, maxLen);
  } else {
    memcpy(out, raw, (size_t)len * sizeof(uint16_t));
    taken = len;
  }
  if (raw != nullptr) delete[] raw;

  if (taken > 0) {
    learnStop();
    return taken;
  }

  receiver->resume();
  if (expired()) {
    timedOut = true;
    learnStop();
  }
  return 0;
}

} // namespace IrIo
