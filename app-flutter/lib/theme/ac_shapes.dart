import 'package:flutter/widgets.dart';

/// Rounded shape language — the BenKon-style "soft card" geometry that replaces
/// the app's former chamfer (cut-corner) look. One place owns the corner radii
/// so every card, nested tile, button, input and pill stays consistent.
///
/// The switch is purely visual: same dark carbon surfaces, same #0055FF accent,
/// same Inter + JetBrains Mono type — only the corners changed from 45° cuts to
/// ~14-16px rounds.
class AcRadii {
  AcRadii._();

  /// Panels / cards — the primary surface radius.
  static const double card = 16;

  /// Nested tiles, buttons, inputs, icon badges.
  static const double inner = 12;

  /// Stadium pills / chips (mode, setpoint, online status).
  static const double pill = 999;
}

BorderRadius get cardRadius => BorderRadius.circular(AcRadii.card);
BorderRadius get innerRadius => BorderRadius.circular(AcRadii.inner);
BorderRadius get pillRadius => BorderRadius.circular(AcRadii.pill);

/// Rounded card shape with an optional border — replaces `ChamferBorder` as the
/// standard `shape:` for Card/Button/ShapeDecoration.
RoundedRectangleBorder acCardShape([BorderSide side = BorderSide.none]) =>
    RoundedRectangleBorder(borderRadius: cardRadius, side: side);

RoundedRectangleBorder acInnerShape([BorderSide side = BorderSide.none]) =>
    RoundedRectangleBorder(borderRadius: innerRadius, side: side);
