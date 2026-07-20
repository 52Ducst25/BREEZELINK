import 'package:dio/dio.dart';
import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:geolocator/geolocator.dart';
import 'package:latlong2/latlong.dart';

/// Result of the picker: the chosen point + its reverse-geocoded address (null
/// if the lookup failed / was offline).
typedef PickedLocation = ({double lat, double lng, String? address});

/// Full-screen OpenStreetMap home-location picker (free, no API key). A fixed
/// centre pin over a draggable map — move the map, the pin marks the chosen
/// point — plus a GPS "current location" button. On confirm it reverse-geocodes
/// via Nominatim and returns [PickedLocation].
class LocationPickerScreen extends StatefulWidget {
  const LocationPickerScreen({super.key, this.initial});
  final LatLng? initial;

  @override
  State<LocationPickerScreen> createState() => _LocationPickerScreenState();
}

class _LocationPickerScreenState extends State<LocationPickerScreen> {
  final MapController _map = MapController();
  bool _busy = false;

  // Fallback centre when there is no saved point and GPS isn't used yet: TP.HCM.
  static const LatLng _fallback = LatLng(10.7769, 106.7009);

  void _snack(String m) {
    if (mounted) ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(m)));
  }

  Future<void> _useGps() async {
    setState(() => _busy = true);
    try {
      if (!await Geolocator.isLocationServiceEnabled()) {
        _snack('Hãy bật Vị trí (GPS) trên điện thoại rồi thử lại.');
        return;
      }
      var perm = await Geolocator.checkPermission();
      if (perm == LocationPermission.denied) perm = await Geolocator.requestPermission();
      if (perm == LocationPermission.denied || perm == LocationPermission.deniedForever) {
        _snack('Cần cấp quyền vị trí để lấy GPS.');
        return;
      }
      final pos = await Geolocator.getCurrentPosition();
      _map.move(LatLng(pos.latitude, pos.longitude), 16);
    } catch (_) {
      _snack('Không lấy được vị trí hiện tại.');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _confirm() async {
    setState(() => _busy = true);
    final c = _map.camera.center;
    String? address;
    try {
      final res = await Dio().get(
        'https://nominatim.openstreetmap.org/reverse',
        queryParameters: {
          'lat': c.latitude,
          'lon': c.longitude,
          'format': 'jsonv2',
          'accept-language': 'vi',
        },
        // Nominatim's usage policy requires an identifying User-Agent.
        options: Options(headers: {'User-Agent': 'AirconApp/1.0 (home-location picker)'}),
      );
      if (res.data is Map) address = res.data['display_name'] as String?;
    } catch (_) {
      // Offline / rate-limited — return the coordinates without a name.
    }
    if (!mounted) return;
    Navigator.pop<PickedLocation>(context, (lat: c.latitude, lng: c.longitude, address: address));
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Chọn vị trí nhà')),
      body: Stack(
        alignment: Alignment.center,
        children: [
          FlutterMap(
            mapController: _map,
            options: MapOptions(
              initialCenter: widget.initial ?? _fallback,
              initialZoom: 15,
              minZoom: 3,
              maxZoom: 19,
            ),
            children: [
              TileLayer(
                urlTemplate: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
                userAgentPackageName: 'com.aircon.app',
              ),
            ],
          ),
          // Fixed centre pin (offset up so its tip marks the map centre).
          const Padding(
            padding: EdgeInsets.only(bottom: 40),
            child: Icon(Icons.location_on, size: 46, color: Color(0xFF0055FF)),
          ),
          Positioned(
            top: 12,
            left: 16,
            right: 16,
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              decoration: BoxDecoration(
                color: Colors.black.withValues(alpha: 0.55),
                borderRadius: BorderRadius.circular(10),
              ),
              child: const Text(
                'Kéo bản đồ để ghim đúng nhà bạn, hoặc bấm nút định vị.',
                textAlign: TextAlign.center,
                style: TextStyle(color: Colors.white, fontSize: 12),
              ),
            ),
          ),
          Positioned(
            right: 16,
            bottom: 92,
            child: FloatingActionButton(
              heroTag: 'gps',
              onPressed: _busy ? null : _useGps,
              child: const Icon(Icons.my_location),
            ),
          ),
          Positioned(
            left: 16,
            right: 16,
            bottom: 24,
            child: FilledButton.icon(
              onPressed: _busy ? null : _confirm,
              icon: _busy
                  ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
                  : const Icon(Icons.check),
              label: const Text('Chọn vị trí này'),
              style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(48)),
            ),
          ),
        ],
      ),
    );
  }
}
