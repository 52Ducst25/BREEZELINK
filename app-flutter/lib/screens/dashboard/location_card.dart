import 'package:flutter/material.dart';

import '../../models/live_reading.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/empty_value.dart';
import '../../widgets/icon_badge.dart';
import '../../widgets/outline_panel.dart';

/// Top-of-dashboard LOCATION card (BenKon "household + outdoor weather"). Shows
/// the household/location name and the OUTDOOR node's live temperature +
/// humidity as an icon + big number + label row. No air-quality figure — we
/// don't measure it. Renders em-dashes when there is no outdoor reading yet,
/// never a fabricated 0.
class LocationCard extends StatelessWidget {
  const LocationCard({super.key, required this.locationName, required this.outdoor});

  final String locationName;
  final LiveReading? outdoor;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return OutlinePanel(
      padding: const EdgeInsets.all(18),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              AcIconBadge(icon: Icons.location_on_outlined, size: 40, iconSize: 20),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(locationName, style: AcText.heading(size: 15, color: ac.white)),
                    Text('Điều kiện ngoài trời', style: AcText.body(size: 12, color: ac.whiteDim)),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: 18),
          Row(
            children: [
              Expanded(
                child: _Metric(
                  icon: Icons.thermostat_outlined,
                  value: outdoor?.temp.toStringAsFixed(0),
                  unit: '°',
                  label: 'Nhiệt độ',
                  color: ac.iceText,
                ),
              ),
              Container(width: 1, height: 44, color: ac.carbonLine),
              Expanded(
                child: _Metric(
                  icon: Icons.water_drop_outlined,
                  value: outdoor?.humidity.toStringAsFixed(0),
                  unit: '%',
                  label: 'Độ ẩm',
                  color: ac.iceText,
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _Metric extends StatelessWidget {
  const _Metric({required this.icon, required this.value, required this.unit, required this.label, required this.color});

  final IconData icon;
  final String? value;
  final String unit;
  final String label;
  final Color color;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        Icon(icon, size: 22, color: ac.whiteDim),
        const SizedBox(width: 10),
        Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            Row(
              crossAxisAlignment: CrossAxisAlignment.baseline,
              textBaseline: TextBaseline.alphabetic,
              children: [
                EmptyValue(value, style: AcText.hero(size: 26, color: color)),
                if (value != null) Text(unit, style: AcText.mono(size: 15, color: color)),
              ],
            ),
            Text(label, style: AcText.body(size: 11.5, color: ac.whiteDim)),
          ],
        ),
      ],
    );
  }
}
