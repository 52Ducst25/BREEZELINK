import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';

import 'ac_colors.dart';
import 'ac_shapes.dart';

/// Material 3 theme: soft ROUNDED corners everywhere (BenKon-style), ice-blue
/// #0055FF accent, dark carbon surfaces. Both light and dark variants attach
/// [AcPalette] so custom widgets read colors via `context.ac.*`.
class AcTheme {
  AcTheme._();

  static ThemeData get dark => _build(Brightness.dark, AcPalette.dark);
  static ThemeData get light => _build(Brightness.light, AcPalette.light);

  static ThemeData _build(Brightness b, AcPalette p) {
    final scheme = ColorScheme(
      brightness: b,
      primary: p.ice,
      onPrimary: b == Brightness.dark ? Colors.black : Colors.white,
      secondary: p.iceText,
      onSecondary: b == Brightness.dark ? Colors.black : Colors.white,
      error: p.error,
      onError: Colors.white,
      surface: p.carbonPanel,
      onSurface: p.white,
    );

    final textTheme = GoogleFonts.interTextTheme(
      (b == Brightness.dark ? ThemeData.dark() : ThemeData.light()).textTheme,
    ).apply(bodyColor: p.white, displayColor: p.white);

    return ThemeData(
      useMaterial3: true,
      brightness: b,
      colorScheme: scheme,
      scaffoldBackgroundColor: p.carbon,
      canvasColor: p.carbon,
      textTheme: textTheme,
      extensions: [p],
      cardTheme: CardThemeData(
        color: p.carbonPanel,
        elevation: 0,
        margin: EdgeInsets.zero,
        shape: acCardShape(BorderSide(color: p.carbonLine, width: 1)),
      ),
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
          backgroundColor: p.ice,
          foregroundColor: Colors.white,
          shape: acInnerShape(),
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 14),
          textStyle: GoogleFonts.inter(fontWeight: FontWeight.w700, letterSpacing: 0.4),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          foregroundColor: p.ice,
          side: BorderSide(color: p.ice, width: 1.5),
          shape: acInnerShape(),
        ),
      ),
      textButtonTheme: TextButtonThemeData(
        style: TextButton.styleFrom(foregroundColor: p.ice),
      ),
      // Rounded inputs to match the soft-card language.
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: p.carbonUp,
        border: OutlineInputBorder(borderRadius: innerRadius),
        enabledBorder: OutlineInputBorder(
          borderRadius: innerRadius,
          borderSide: BorderSide(color: p.carbonLine, width: 1.5),
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: innerRadius,
          borderSide: BorderSide(color: p.ice, width: 1.5),
        ),
        errorBorder: OutlineInputBorder(
          borderRadius: innerRadius,
          borderSide: BorderSide(color: p.error, width: 1.5),
        ),
        labelStyle: TextStyle(color: p.whiteDim),
      ),
      appBarTheme: AppBarTheme(
        backgroundColor: p.carbonPanel,
        foregroundColor: p.white,
        elevation: 0,
        centerTitle: false,
      ),
      navigationBarTheme: NavigationBarThemeData(
        backgroundColor: p.carbonPanel,
        indicatorColor: p.iceDim,
        labelTextStyle: WidgetStateProperty.resolveWith(
          (s) => GoogleFonts.inter(
            fontSize: 11,
            fontWeight: FontWeight.w600,
            color: s.contains(WidgetState.selected) ? p.ice : p.whiteDim,
          ),
        ),
      ),
      dividerTheme: DividerThemeData(color: p.carbonLine, thickness: 1),
      switchTheme: SwitchThemeData(
        thumbColor: WidgetStateProperty.resolveWith(
          (s) => s.contains(WidgetState.selected) ? p.ice : p.whiteDim,
        ),
        trackColor: WidgetStateProperty.resolveWith(
          (s) => s.contains(WidgetState.selected) ? p.iceDim : p.carbonLineBright,
        ),
      ),
      sliderTheme: SliderThemeData(
        activeTrackColor: p.ice,
        inactiveTrackColor: p.carbonLine,
        thumbColor: p.ice,
        overlayColor: p.iceGlow,
      ),
      progressIndicatorTheme: ProgressIndicatorThemeData(color: p.ice),
      snackBarTheme: SnackBarThemeData(
        backgroundColor: p.carbonUp,
        contentTextStyle: TextStyle(color: p.white),
        shape: acInnerShape(),
      ),
    );
  }
}
