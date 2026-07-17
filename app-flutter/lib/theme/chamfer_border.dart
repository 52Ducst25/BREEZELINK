import 'package:flutter/material.dart';

/// Chamfer (45-degree cut corner) outline border — the app's signature shape,
/// ported verbatim from the SafeKitchen "Titanium Command" design language.
/// Cuts the TOP-LEFT and BOTTOM-RIGHT corners; no rounded corners anywhere.
///
/// Used as `shape:` for Card/Button/Container(decoration: ShapeDecoration).
/// [cut] follows the design tokens: sm=8, md=12, lg=20.
class ChamferBorder extends OutlinedBorder {
  const ChamferBorder({super.side = BorderSide.none, this.cut = 12});

  final double cut;

  Path _path(Rect rect) {
    final c = cut.clamp(0.0, rect.shortestSide / 2);
    return Path()
      ..moveTo(rect.left + c, rect.top)
      ..lineTo(rect.right, rect.top)
      ..lineTo(rect.right, rect.bottom - c)
      ..lineTo(rect.right - c, rect.bottom)
      ..lineTo(rect.left, rect.bottom)
      ..lineTo(rect.left, rect.top + c)
      ..close();
  }

  @override
  Path getInnerPath(Rect rect, {TextDirection? textDirection}) =>
      _path(rect.deflate(side.width));

  @override
  Path getOuterPath(Rect rect, {TextDirection? textDirection}) => _path(rect);

  @override
  void paint(Canvas canvas, Rect rect, {TextDirection? textDirection}) {
    if (side.style == BorderStyle.none) return;
    canvas.drawPath(getOuterPath(rect.deflate(side.width / 2)), side.toPaint());
  }

  @override
  ChamferBorder copyWith({BorderSide? side, double? cut}) =>
      ChamferBorder(side: side ?? this.side, cut: cut ?? this.cut);

  @override
  ShapeBorder scale(double t) => ChamferBorder(side: side.scale(t), cut: cut * t);

  @override
  EdgeInsetsGeometry get dimensions => EdgeInsets.all(side.width);
}
