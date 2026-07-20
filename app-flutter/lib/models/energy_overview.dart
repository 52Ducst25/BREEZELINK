import 'parse_utils.dart';

/// Mirrors backend `GET /api/v1/energy/overview`. EVERY number is nullable —
/// the meter may have no reading yet — and the UI must render "Chưa có dữ liệu
/// điện" for a null total rather than a fabricated 0 kWh / 0 %.
class EnergyOverview {
  const EnergyOverview({
    required this.totalKwhToday,
    required this.pctVsYesterday,
    required this.devices,
  });

  final double? totalKwhToday;
  final double? pctVsYesterday;
  final List<DeviceEnergy> devices;

  /// True when there is genuinely no energy figure to show yet.
  bool get isEmpty => totalKwhToday == null && devices.isEmpty;

  factory EnergyOverview.fromJson(Map<String, dynamic> json) {
    final rows = (json['devices'] as List?) ?? const [];
    return EnergyOverview(
      totalKwhToday: asDoubleOrNull(json['total_kwh_today']),
      pctVsYesterday: asDoubleOrNull(json['pct_vs_yesterday']),
      devices: rows
          .whereType<Map<String, dynamic>>()
          .map(DeviceEnergy.fromJson)
          .toList(),
    );
  }
}

/// One device's energy line in the overview.
class DeviceEnergy {
  const DeviceEnergy({
    required this.deviceId,
    required this.name,
    required this.kwhToday,
    required this.pctVsYesterday,
  });

  final String deviceId;
  final String name;
  final double? kwhToday;
  final double? pctVsYesterday;

  factory DeviceEnergy.fromJson(Map<String, dynamic> json) {
    return DeviceEnergy(
      deviceId: asString(json['device_id']),
      name: asString(json['name'], 'Thiết bị'),
      kwhToday: asDoubleOrNull(json['kwh_today']),
      pctVsYesterday: asDoubleOrNull(json['pct_vs_yesterday']),
    );
  }
}
