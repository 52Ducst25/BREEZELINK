import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';

import 'ac_colors.dart';
import 'chamfer_border.dart';

/// "Glacier Command" Material 3 theme: chamfer-cut corners everywhere (no
/// rounded corners), square 2px-bordered inputs, ice-blue accent. Both light
/// and dark variants attach [AcPalette] so custom widgets read colors via
/// `context.ac.*`.
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
        shape: ChamferBorder(cut: 12, side: BorderSide(color: p.carbonLine, width: 2)),
      ),
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
          backgroundColor: p.ice,
          foregroundColor: b == Brightness.dark ? Colors.black : Colors.white,
          shape: const ChamferBorder(cut: 8),
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 14),
          textStyle: GoogleFonts.inter(fontWeight: FontWeight.w700, letterSpacing: 0.4),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          foregroundColor: p.ice,
          side: BorderSide(color: p.ice, width: 2),
          shape: const ChamferBorder(cut: 8),
        ),
      ),
      textButtonTheme: TextButtonThemeData(
        style: TextButton.styleFrom(foregroundColor: p.ice),
      ),
      // Square inputs, 2px borders — no rounded corners per the brief.
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: p.carbonUp,
        border: const OutlineInputBorder(borderRadius: BorderRadius.zero),
        enabledBorder: OutlineInputBorder(
          borderRadius: BorderRadius.zero,
          borderSide: BorderSide(color: p.carbonLine, width: 2),
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: BorderRadius.zero,
          borderSide: BorderSide(color: p.ice, width: 2),
        ),
        errorBorder: OutlineInputBorder(
          borderRadius: BorderRadius.zero,
          borderSide: BorderSide(color: p.error, width: 2),
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
        shape: const ChamferBorder(cut: 8),
      ),
    );
  }
}
