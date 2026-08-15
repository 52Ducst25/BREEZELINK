#pragma once
#include <Arduino.h>

// ============================================================================
//  Infrared receive / transmit (3-pin "IR Receiver" + "IR Transmitter" modules).
// ----------------------------------------------------------------------------
//  Two flows share this hardware:
//
//  TRANSMIT -- the backend sends a cmd containing a timing array, and the node
//              replays it verbatim at the air conditioner. No per-brand decoding:
//              every brand has its own protocol, and simply replaying the learned
//              waveform works with all of them.
//
//  LEARN    -- the backend sends {"learn":"COOL 25"}, the node enables the
//              receiver and waits for the user to press the real remote, captures
//              the waveform and uploads it to the learn topic. This is what makes
//              supporting a new air conditioner brand require NO reflashing.
//
//  The receiver is only enabled during LEARN, never permanently: the transmitter
//  LED sits right next to the receiver on the same board, and with both enabled
//  the node captures the very frame it just transmitted.
// ============================================================================
namespace IrIo {

/// The longest command frame accepted (number of mark/space transitions). An air
/// conditioner remote sends its entire state (mode+temp+fan+timer) in one frame,
/// so it is much longer than a TV remote's -- 600 transitions covers every common
/// brand while still costing only 1.2KB of RAM.
static const uint16_t RAW_MAX = 600;

void begin(uint8_t txPin, uint8_t rxPin);

/// Transmit one frame at the air conditioner (38kHz carrier). Blocks for
/// ~50-250ms depending on the frame length.
void blast(const uint16_t *raw, uint16_t len);

/// Enter learn mode, waiting at most [timeoutMs].
void learnStart(uint32_t timeoutMs);

bool learning();

/// How many milliseconds remain before the wait times out. 0 when not learning.
/// It exists so the screen can count down for whoever is holding the remote --
/// without it they only see a line of static text and cannot tell whether they
/// still have time or have already timed out.
uint32_t learnRemainingMs();

/// Call every loop(). Returns how many transitions were just captured (>0 = done,
/// and it leaves learn mode by itself), 0 = nothing yet. Noise and abnormally long
/// frames are discarded and learning continues.
uint16_t learnPoll(uint16_t *out, uint16_t maxLen);

/// true exactly ONCE, right after the wait times out without capturing anything.
bool learnTimedOut();

void learnStop();

} // namespace IrIo
