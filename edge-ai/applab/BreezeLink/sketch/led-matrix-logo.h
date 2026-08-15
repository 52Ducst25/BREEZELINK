/*
  An animated Qualcomm logo on the UNO Q's 13x8 LED matrix.

  A three-phase loop, 13 seconds in total:

      DRAW 5s          the stroke grows from the top, around the ring clockwise,
                       with the Q's tail appearing last - the way it is written by hand
      HOLD 3s          still and fully lit - giving the eye time to read the Q
      ERASE 5s         the stroke dissolves along the same path but in reverse: the
                       tail goes first, then the ring unwinds anticlockwise back to
                       the top

  SPLIT OUT OF sketch.ino because it has nothing to do with the data path: the main
  sketch handles UART and RPC, while this is purely display. Merged into one file,
  every protocol debugging session would mean scrolling past two tables of 104 numbers.
*/

#pragma once

#include <stdint.h>

namespace LedLogo {

/// Turn on the matrix and start the loop from a blank screen. Call once in setup().
void begin();

/// Advance the loop. Call every loop() -- the function keeps its own time, and only
/// the DRAW phase actually renders (25 frames/second); the other two phases render
/// exactly once on entry and then let the matrix's own scan circuit hold the image.
///
/// CALL IT AFTER pump(): Zephyr's UART buffer is finite, and a 39-byte frame arriving
/// while we are busy rendering is a lost frame.
void update();

}  // namespace LedLogo
