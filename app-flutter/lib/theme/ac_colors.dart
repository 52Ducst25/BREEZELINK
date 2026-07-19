import 'package:flutter/material.dart';

/// Aircon palette — the app half of the "Titanium Command" brand shared with
/// the vendor web admin. Same chamfer geometry, same Inter + JetBrains Mono
/// type, same 2px square borders — and, as of the brand-consistency pass, the
/// SAME accent: web tech-blue #0055FF (was ice #33C7FF). The token is still
/// named `ice` for continuity across ~114 call sites; it is now the brand blue,
/// not a cyan. Dark base by default (a phone AC app reads best dark), but the
/// light variant below matches the web's light surfaces for anyone who prefers
/// it. Thermal colors (cold/neutral/warm/hot) stay the semantic core: they
/// encode WHAT A READING MEANS, never WHICH SENSOR produced it.
class AcColors {
  AcColors._();

  // --- Dark surfaces ---
  static const carbon = Color(0xFF0A0E14); // body background
  static const carbonUp = Color(0xFF121924); // hover / raised layer
  static const carbonPanel = Color(0xFF141C28); // card / panel
  static const carbonLine = Color(0xFF2A3B4C); // border
  static const carbonLineBright = Color(0xFF3E5468); // brighter border

  // --- Brand accent: web tech-blue #0055FF ---
  // On the dark carbon base #0055FF is fine as a FILL (white text over it) and
  // as a border, but too dark for small text — so `iceText` is a brighter tint
  // of the same hue for links / emphasized numerals on dark.
  static const ice = Color(0xFF0055FF); // primary accent (brand blue)
  static const iceText = Color(0xFF4D8DFF); // links / emphasized numerals (dark-readable)
  static const iceDim = Color(0x260055FF); // ~15% soft background
  static const iceGhost = Color(0x140055FF); // ~8% row hover
  static const iceGlow = Color(0x330055FF); // ~20% glow

  // --- Text & generic status ---
  static const white = Color(0xFFE7F1F8);
  static const whiteDim = Color(0xFF8DA2B5);
  static const success = Color(0xFF22C55E);
  static const error = Color(0xFFFF4D4D);
  static const warning = Color(0xFFF5A623);

  // --- Thermal-state scale: cold -> neutral -> warm -> hot ---
  // Applied to ANY temperature reading by value, never by sensor identity.
  static const thermalCold = Color(0xFF3AA0FF); // below comfort band
  static const thermalNeutral = Color(0xFF22C55E); // inside comfort band
  static const thermalWarm = Color(0xFFF5A623); // above comfort band
  static const thermalHot = Color(0xFFFF4D4D); // well above / danger
}

/// Theme-aware palette exposed via [ThemeExtension] so widgets read
/// `context.ac.<token>` instead of static constants (dark/light both defined).
@immutable
class AcPalette extends ThemeExtension<AcPalette> {
  const AcPalette({
    required this.carbon,
    required this.carbonUp,
    required this.carbonPanel,
    required this.carbonLine,
    required this.carbonLineBright,
    required this.ice,
    required this.iceText,
    required this.iceDim,
    required this.iceGhost,
    required this.iceGlow,
    required this.white,
    required this.whiteDim,
    required this.success,
    required this.error,
    required this.warning,
    required this.thermalCold,
    required this.thermalNeutral,
    required this.thermalWarm,
    required this.thermalHot,
  });

  final Color carbon, carbonUp, carbonPanel, carbonLine, carbonLineBright;
  final Color ice, iceText, iceDim, iceGhost, iceGlow;
  final Color white, whiteDim, success, error, warning;
  final Color thermalCold, thermalNeutral, thermalWarm, thermalHot;

  static const dark = AcPalette(
    carbon: AcColors.carbon,
    carbonUp: AcColors.carbonUp,
    carbonPanel: AcColors.carbonPanel,
    carbonLine: AcColors.carbonLine,
    carbonLineBright: AcColors.carbonLineBright,
    ice: AcColors.ice,
    iceText: AcColors.iceText,
    iceDim: AcColors.iceDim,
    iceGhost: AcColors.iceGhost,
    iceGlow: AcColors.iceGlow,
    white: AcColors.white,
    whiteDim: AcColors.whiteDim,
    success: AcColors.success,
    error: AcColors.error,
    warning: AcColors.warning,
    thermalCold: AcColors.thermalCold,
    thermalNeutral: AcColors.thermalNeutral,
    thermalWarm: AcColors.thermalWarm,
    thermalHot: AcColors.thermalHot,
  );

  /// Light variant — matches the web's light surfaces (page #EFF4F8, white
  /// cards, deep ink text) with the SAME brand blue #0055FF accent, so the two
  /// themes are one brand in either mode.
  static const light = AcPalette(
    carbon: Color(0xFFEFF4F8),
    carbonUp: Color(0xFFF8FBFD),
    carbonPanel: Color(0xFFFFFFFF),
    carbonLine: Color(0xFFC9D8E3),
    carbonLineBright: Color(0xFFDCE7EF),
    ice: Color(0xFF0055FF),
    iceText: Color(0xFF0044CC),
    iceDim: Color(0x1A0055FF),
    iceGhost: Color(0x0D0055FF),
    iceGlow: Color(0x260055FF),
    white: Color(0xFF0F1B24),
    whiteDim: Color(0xFF5B7285),
    success: Color(0xFF16A34A),
    error: Color(0xFFE0342B),
    warning: Color(0xFFCB790C),
    thermalCold: Color(0xFF1E7FD6),
    thermalNeutral: Color(0xFF16A34A),
    thermalWarm: Color(0xFFCB790C),
    thermalHot: Color(0xFFE0342B),
  );

  @override
  AcPalette copyWith({
    Color? carbon,
    Color? carbonUp,
    Color? carbonPanel,
    Color? carbonLine,
    Color? carbonLineBright,
    Color? ice,
    Color? iceText,
    Color? iceDim,
    Color? iceGhost,
    Color? iceGlow,
    Color? white,
    Color? whiteDim,
    Color? success,
    Color? error,
    Color? warning,
    Color? thermalCold,
    Color? thermalNeutral,
    Color? thermalWarm,
    Color? thermalHot,
  }) {
    return AcPalette(
      carbon: carbon ?? this.carbon,
      carbonUp: carbonUp ?? this.carbonUp,
      carbonPanel: carbonPanel ?? this.carbonPanel,
      carbonLine: carbonLine ?? this.carbonLine,
      carbonLineBright: carbonLineBright ?? this.carbonLineBright,
      ice: ice ?? this.ice,
      iceText: iceText ?? this.iceText,
      iceDim: iceDim ?? this.iceDim,
      iceGhost: iceGhost ?? this.iceGhost,
      iceGlow: iceGlow ?? this.iceGlow,
      white: white ?? this.white,
      whiteDim: whiteDim ?? this.whiteDim,
      success: success ?? this.success,
      error: error ?? this.error,
      warning: warning ?? this.warning,
      thermalCold: thermalCold ?? this.thermalCold,
      thermalNeutral: thermalNeutral ?? this.thermalNeutral,
      thermalWarm: thermalWarm ?? this.thermalWarm,
      thermalHot: thermalHot ?? this.thermalHot,
    );
  }

  @override
  AcPalette lerp(ThemeExtension<AcPalette>? other, double t) {
    if (other is! AcPalette) return this;
    Color l(Color a, Color b) => Color.lerp(a, b, t)!;
    return AcPalette(
      carbon: l(carbon, other.carbon),
      carbonUp: l(carbonUp, other.carbonUp),
      carbonPanel: l(carbonPanel, other.carbonPanel),
      carbonLine: l(carbonLine, other.carbonLine),
      carbonLineBright: l(carbonLineBright, other.carbonLineBright),
      ice: l(ice, other.ice),
      iceText: l(iceText, other.iceText),
      iceDim: l(iceDim, other.iceDim),
      iceGhost: l(iceGhost, other.iceGhost),
      iceGlow: l(iceGlow, other.iceGlow),
      white: l(white, other.white),
      whiteDim: l(whiteDim, other.whiteDim),
      success: l(success, other.success),
      error: l(error, other.error),
      warning: l(warning, other.warning),
      thermalCold: l(thermalCold, other.thermalCold),
      thermalNeutral: l(thermalNeutral, other.thermalNeutral),
      thermalWarm: l(thermalWarm, other.thermalWarm),
      thermalHot: l(thermalHot, other.thermalHot),
    );
  }
}

/// `context.ac.<token>` — falls back to [AcPalette.dark] if unattached (tests).
extension AcContext on BuildContext {
  AcPalette get ac => Theme.of(this).extension<AcPalette>() ?? AcPalette.dark;
}
