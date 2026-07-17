/// Safe casters for JSON values coming off the wire. A tunnel/proxy can
/// return HTML with a 200, or a field can arrive as a different JSON number
/// type than expected — never blind-cast (`as double`), always go through
/// these so a malformed body degrades to a fallback instead of throwing deep
/// inside a model constructor.
library;

double? asDoubleOrNull(dynamic v) {
  if (v == null) return null;
  if (v is num) return v.toDouble();
  if (v is String) return double.tryParse(v);
  return null;
}

double asDouble(dynamic v, [double fallback = 0]) => asDoubleOrNull(v) ?? fallback;

int? asIntOrNull(dynamic v) {
  if (v == null) return null;
  if (v is num) return v.round();
  if (v is String) return int.tryParse(v) ?? double.tryParse(v)?.round();
  return null;
}

int asInt(dynamic v, [int fallback = 0]) => asIntOrNull(v) ?? fallback;

bool asBool(dynamic v, [bool fallback = false]) {
  if (v is bool) return v;
  if (v is num) return v != 0;
  if (v is String) return v.toLowerCase() == 'true' || v == '1';
  return fallback;
}

String? asStringOrNull(dynamic v) => v == null ? null : v.toString();

String asString(dynamic v, [String fallback = '']) => v?.toString() ?? fallback;

/// Parse an ISO-8601 timestamp; null on absent/malformed input rather than
/// throwing (server clock skew / partial rows should not crash a screen).
DateTime? asDateTimeOrNull(dynamic v) {
  if (v == null) return null;
  if (v is String) return DateTime.tryParse(v);
  return null;
}
