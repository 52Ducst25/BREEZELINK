import 'package:flutter/material.dart';

import '../theme/ac_text.dart';

/// Renders a numeric/text measurement, or an em-dash when [value] is null.
///
/// CRITICAL correctness rule (brief): absence of data is not a reading of
/// zero. `comfort.t_set`/`mode` are legitimately null while
/// `data_available=false` or the IR table hasn't been learned yet — this
/// widget is the ONE place that decides how "no value" renders, so no screen
/// can accidentally fall back to `?? 0` and fabricate a number.
class EmptyValue extends StatelessWidget {
  const EmptyValue(this.value, {super.key, this.style, this.suffix = ''});

  /// Pre-formatted string (caller already did `.toStringAsFixed(...)` etc.)
  /// or null to render the em-dash placeholder.
  final String? value;
  final TextStyle? style;
  final String suffix;

  static const _dash = '—';

  @override
  Widget build(BuildContext context) {
    final text = value == null ? _dash : '$value$suffix';
    return Text(text, style: style ?? AcText.mono());
  }
}
