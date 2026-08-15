#include "led-matrix-logo.h"

#include <Arduino.h>
#include <Arduino_LED_Matrix.h>

namespace LedLogo {
namespace {

// The UNO Q's matrix: 13 columns x 8 rows, 3-bit greyscale (0..7).
constexpr uint8_t COLS = 13;
constexpr uint8_t ROWS = 8;
constexpr uint16_t CELLS = COLS * ROWS;

// --- The loop's timing --------------------------------------------------------
//
//   DRAW (5s)  ->  HOLD (3s)  ->  ERASE (5s)  ->  repeat   - a full 13s cycle
//
// The HOLD phase in the middle gives the eye a pause to read the Q before it starts
// dissolving - without it the stroke dissolves the instant it closes, and the complete
// logo exists for exactly one frame.
constexpr uint32_t DRAW_MS = 5000;
constexpr uint32_t HOLD_MS = 3000;
constexpr uint32_t ERASE_MS = 5000;

// 40ms/frame = 25 frames/second, used only by the two ANIMATED phases. The HOLD phase
// is static so it renders exactly once on entry - the matrix has its own
// interrupt-driven scan circuit, and once a frame is loaded it holds it without costing
// the loop a single cycle.
//
// Most frames will be identical to the previous one - 40 lit cells spread across 255
// steps means a cell changes state every 125ms on average. We still render at 25
// frames/second because building a frame is only 104 comparisons, far cheaper than
// having to remember how far the previous frame got.
constexpr uint32_t FRAME_MS = 40;

// Set to 0 for a static, permanently lit logo.
#define LOGO_ANIMATE 1

// --- Tables -------------------------------------------------------------------
//
// Qualcomm's Q. Generated with a distance function (a ring plus a line segment for the
// tail) and quantised to 8 levels - see the ASCII rendering on the right.
//
// Cells with values 2-5 are NOT "dim", they are antialiasing: they sit on the edge of
// the stroke and they are what makes an 8-pixel circle actually look like a circle.
const uint8_t LOGO[CELLS] = {
    0, 0, 0, 0, 2, 5, 4, 0, 0, 0, 0, 0, 0,  //     :+=
    0, 0, 0, 6, 7, 5, 6, 7, 3, 0, 0, 0, 0,  //    *#+*#-
    0, 0, 3, 7, 0, 0, 0, 2, 7, 0, 0, 0, 0,  //   -#   :#
    0, 0, 7, 3, 0, 0, 0, 0, 7, 2, 0, 0, 0,  //   #-    #:
    0, 0, 7, 3, 0, 0, 4, 3, 7, 2, 0, 0, 0,  //   #-  =-#:
    0, 0, 3, 7, 0, 0, 1, 7, 7, 0, 0, 0, 0,  //   -#  .##
    0, 0, 0, 6, 7, 5, 6, 7, 7, 4, 0, 0, 0,  //    *#+*##=
    0, 0, 0, 0, 2, 5, 4, 0, 0, 7, 5, 0, 0,  //     :+=  #+
};

// Each cell's appearance threshold, on a 0..255 scale. A cell whose threshold is <= the
// progress value has been drawn.
//
// THE ORDER IS HOW A PERSON WRITES A Q BY HAND: around the ring first, starting at the
// top and going clockwise (steps 0..200), and only then the tail (200..255). Letting the
// tail appear while the ring is still incomplete makes it look like noise rather than a
// pen stroke.
//
// A LOOKUP TABLE RATHER THAN TRIGONOMETRY AT RUNTIME: each frame costs one comparison
// per cell, on a microcontroller that also has to read a UART without dropping a byte.
//
// The 255s on unlit cells are harmless - they are short-circuited by the earlier
// LOGO[i] == 0 check.
const uint8_t ORDER[CELLS] = {
    255, 255, 255, 255, 189, 197,   6, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 176, 185, 196,   9,  19,  26, 255, 255, 255, 255,
    255, 255, 164, 168, 255, 255, 255,  27,  34, 255, 255, 255, 255,
    255, 255, 155, 157, 255, 255, 255, 255,  44,  46, 255, 255, 255,
    255, 255, 145, 143, 255, 255, 200, 203,  56,  54, 255, 255, 255,
    255, 255, 136, 132, 255, 255, 202, 212,  66, 255, 255, 255, 255,
    255, 255, 255, 124, 115, 104,  91,  81, 230, 239, 255, 255, 255,
    255, 255, 255, 255, 111, 103,  94, 255, 255, 248, 255, 255, 255,
};

Arduino_LED_Matrix matrix;

uint8_t frame[CELLS];

enum Phase : uint8_t { PHASE_DRAW, PHASE_HOLD, PHASE_ERASE };
Phase phase = PHASE_DRAW;
uint32_t phaseStartMs = 0;
uint32_t lastFrameMs = 0;

/// Render the stroke's partial state: only the cells whose turn has come.
///
/// THE ERASE PHASE USES THIS EXACT FUNCTION, differing only in that the progress runs
/// from 255 down to 0. That makes the stroke dissolve along precisely the path it grew
/// along, just in reverse - the Q's tail vanishes first, then the ring unwinds
/// anticlockwise back to the top. Writing a separate erase function would almost
/// certainly let the two paths diverge somewhere.
void renderProgress(uint8_t progress) {
  for (uint16_t i = 0; i < CELLS; i++) {
    frame[i] = (LOGO[i] != 0 && ORDER[i] <= progress) ? LOGO[i] : 0;
  }
  matrix.draw(frame);
}

}  // namespace

void begin() {
  matrix.begin();
  // MANDATORY before draw(): by default the library interprets the buffer as a
  // 256-level scale, so our 0..7 table would come out almost completely dark.
  matrix.setGrayscaleBits(3);

#if LOGO_ANIMATE
  phase = PHASE_DRAW;
  phaseStartMs = millis();
  renderProgress(0);        // start from a blank screen and let the stroke grow
#else
  matrix.draw(LOGO);
#endif
}

void update() {
#if LOGO_ANIMATE
  const uint32_t now = millis();
  const uint32_t elapsed = now - phaseStartMs;

  switch (phase) {
    case PHASE_DRAW:
      if (elapsed >= DRAW_MS) {
        phase = PHASE_HOLD;
        phaseStartMs = now;
        matrix.draw(LOGO);          // close the stroke, then hold still for the HOLD phase
        return;
      }
      if (now - lastFrameMs < FRAME_MS) return;
      lastFrameMs = now;
      // Multiply before dividing so no resolution is lost. elapsed < 5000, so the
      // maximum product is 4999x255 ~= 1.27 million - comfortably inside a uint32, no
      // overflow.
      renderProgress((uint8_t)(elapsed * 255u / DRAW_MS));
      return;

    case PHASE_HOLD:
      // Static: nothing is redrawn, we simply wait out the phase.
      if (elapsed >= HOLD_MS) {
        phase = PHASE_ERASE;
        phaseStartMs = now;
        lastFrameMs = 0;
      }
      return;

    case PHASE_ERASE:
      if (elapsed >= ERASE_MS) {
        phase = PHASE_DRAW;
        phaseStartMs = now;
        lastFrameMs = 0;
        renderProgress(0);          // fully cleared before drawing again from scratch
        return;
      }
      if (now - lastFrameMs < FRAME_MS) return;
      lastFrameMs = now;
      renderProgress((uint8_t)(255u - elapsed * 255u / ERASE_MS));
      return;
  }
#endif
}

}  // namespace LedLogo
