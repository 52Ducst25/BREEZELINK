import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';

/// Text style kit. Body copy uses Inter; every MEASUREMENT (temperature,
/// humidity, setpoint, ppm-like figures) uses JetBrains Mono with tabular
/// figures so columns of numbers align — required by the brief for all
/// measurement display, not just chart axes.
class AcText {
  AcText._();

  /// Big hero numeral (setpoint / current temp) — mono, tabular.
  static TextStyle hero({double size = 56, Color? color, FontWeight weight = FontWeight.w600}) =>
      GoogleFonts.jetBrainsMono(
        fontSize: size,
        fontWeight: weight,
        color: color,
        fontFeatures: const [FontFeature.tabularFigures()],
        letterSpacing: -1,
      );

  /// Inline measurement value — mono, tabular (pipeline read-out rows, tiles).
  static TextStyle mono({double size = 14, Color? color, FontWeight weight = FontWeight.w500}) =>
      GoogleFonts.jetBrainsMono(
        fontSize: size,
        fontWeight: weight,
        color: color,
        fontFeatures: const [FontFeature.tabularFigures()],
      );

  /// Card/section heading — Inter semibold.
  static TextStyle heading({double size = 16, Color? color}) => GoogleFonts.inter(
        fontSize: size,
        fontWeight: FontWeight.w600,
        color: color,
        letterSpacing: -0.1,
      );

  /// Small overline label — Inter medium, wide tracking.
  static TextStyle label({double size = 11, Color? color}) => GoogleFonts.inter(
        fontSize: size,
        color: color,
        letterSpacing: 0.4,
        fontWeight: FontWeight.w500,
      );

  /// Body / description text — Inter regular.
  static TextStyle body({double size = 13, Color? color}) => GoogleFonts.inter(
        fontSize: size,
        color: color,
        height: 1.5,
        fontWeight: FontWeight.w400,
      );
}
