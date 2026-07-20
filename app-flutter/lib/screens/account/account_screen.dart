import 'package:flutter/material.dart';
import 'package:latlong2/latlong.dart';
import 'package:provider/provider.dart';

import '../../services/api_client.dart';
import 'location_picker_screen.dart';
import '../../state/app_state.dart';
import '../../state/theme_controller.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/outline_panel.dart';
import '../../widgets/primary_button.dart';
import '../../widgets/section_label.dart';
import '../../widgets/update_prompt.dart';

/// TÀI KHOẢN tab: identity (from `/auth/me`), server address, theme toggle,
/// logout. Changing server requires logging out first (server URL is fixed
/// for the duration of a session, same as SafeKitchen).
class AccountScreen extends StatelessWidget {
  const AccountScreen({super.key, required this.onLogout});

  final VoidCallback onLogout;

  @override
  Widget build(BuildContext context) {
    final s = context.watch<AppState>();
    final ac = context.ac;
    final profile = s.profile;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Text('Tài khoản', style: AcText.heading(size: 18, color: ac.white)),
        const SizedBox(height: 16),
        const SectionLabel('Hồ sơ'),
        const SizedBox(height: 10),
        OutlinePanel(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              _row(ac, 'Email', profile?.email ?? '—', icon: Icons.alternate_email),
              _row(ac, 'Vai trò', profile == null ? '—' : (profile.isOwner ? 'Chủ sở hữu' : 'Thành viên'),
                  icon: Icons.badge_outlined),
              _row(ac, 'Máy chủ', s.apiClient.baseUrl, icon: Icons.dns_outlined),
            ],
          ),
        ),
        const SizedBox(height: 20),
        const SectionLabel('Địa chỉ nhà'),
        const SizedBox(height: 10),
        _HomeAddressEditor(
          initial: profile?.location ?? '',
          initialLat: profile?.latitude,
          initialLng: profile?.longitude,
        ),
        const SizedBox(height: 20),
        const SectionLabel('Giao diện'),
        const SizedBox(height: 10),
        const _ThemeModeSelector(),
        const SizedBox(height: 20),
        const SectionLabel('Ứng dụng'),
        const SizedBox(height: 10),
        OutlinePanel(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              _row(ac, 'Phiên bản', ApiClient.appVersion.isEmpty ? '—' : ApiClient.appVersion,
                  icon: Icons.info_outline),
              const SizedBox(height: 10),
              Align(
                alignment: Alignment.centerLeft,
                child: PrimaryButton(
                  label: 'Kiểm tra cập nhật',
                  icon: Icons.system_update_outlined,
                  primary: false,
                  onPressed: () => checkForUpdate(context, s.apiClient.baseUrl),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 20),
        const SectionLabel('Phiên đăng nhập'),
        const SizedBox(height: 10),
        Align(
          alignment: Alignment.centerLeft,
          child: PrimaryButton(label: 'Đăng xuất', icon: Icons.logout, primary: false, onPressed: () => _confirmLogout(context)),
        ),
      ],
    );
  }

  Widget _row(AcPalette ac, String label, String value, {IconData? icon}) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 6),
        child: Row(
          children: [
            if (icon != null) ...[Icon(icon, size: 16, color: ac.whiteDim), const SizedBox(width: 10)],
            Text(label, style: AcText.body(size: 12.5, color: ac.whiteDim)),
            const Spacer(),
            Flexible(child: Text(value, style: AcText.mono(size: 12.5, color: ac.white), textAlign: TextAlign.right)),
          ],
        ),
      );

  Future<void> _confirmLogout(BuildContext context) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Đăng xuất'),
        content: const Text('Bạn có chắc muốn đăng xuất khỏi tài khoản này?'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Hủy')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Đăng xuất')),
        ],
      ),
    );
    if (ok == true) onLogout();
  }
}

/// Editable household address. Saved to the profile (PUT /auth/me) and shown on
/// the dashboard location card (BenKon-style). Its own StatefulWidget for the
/// text field + save state; the account screen itself stays stateless.
class _HomeAddressEditor extends StatefulWidget {
  const _HomeAddressEditor({required this.initial, this.initialLat, this.initialLng});
  final String initial;
  final double? initialLat;
  final double? initialLng;

  @override
  State<_HomeAddressEditor> createState() => _HomeAddressEditorState();
}

class _HomeAddressEditorState extends State<_HomeAddressEditor> {
  late final TextEditingController _ctrl = TextEditingController(text: widget.initial);
  late double? _lat = widget.initialLat;
  late double? _lng = widget.initialLng;
  bool _saving = false;

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  Future<void> _save({double? lat, double? lng}) async {
    setState(() => _saving = true);
    final err = await context.read<AppState>().updateProfile(
          location: _ctrl.text.trim(),
          latitude: lat,
          longitude: lng,
        );
    if (!mounted) return;
    setState(() => _saving = false);
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text(err ?? 'Đã lưu địa chỉ nhà')),
    );
  }

  Future<void> _pickOnMap() async {
    final r = await Navigator.of(context).push<PickedLocation>(
      MaterialPageRoute(
        builder: (_) => LocationPickerScreen(
          initial: (_lat != null && _lng != null) ? LatLng(_lat!, _lng!) : null,
        ),
      ),
    );
    if (r == null || !mounted) return;
    setState(() {
      _lat = r.lat;
      _lng = r.lng;
      _ctrl.text = (r.address != null && r.address!.isNotEmpty)
          ? r.address!
          : '${r.lat.toStringAsFixed(5)}, ${r.lng.toStringAsFixed(5)}';
    });
    await _save(lat: r.lat, lng: r.lng); // persist the picked point + its address
  }

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return OutlinePanel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          TextField(
            controller: _ctrl,
            textInputAction: TextInputAction.done,
            onSubmitted: (_) => _save(lat: _lat, lng: _lng),
            style: AcText.body(size: 13.5, color: ac.white),
            decoration: const InputDecoration(
              hintText: 'VD: Linh Trung, Quận Thủ Đức',
              prefixIcon: Icon(Icons.location_on_outlined, size: 18),
              isDense: true,
            ),
          ),
          const SizedBox(height: 6),
          Text('Nhập tay, hoặc chọn trên bản đồ. Địa chỉ hiện trên thẻ vị trí ở trang chủ.',
              style: AcText.body(size: 11, color: ac.whiteDim)),
          const SizedBox(height: 10),
          Row(
            children: [
              PrimaryButton(label: 'Chọn trên bản đồ', icon: Icons.map_outlined, primary: false, onPressed: _pickOnMap),
              const Spacer(),
              PrimaryButton(
                label: _saving ? 'Đang lưu…' : 'Lưu',
                icon: Icons.save_outlined,
                onPressed: _saving ? null : () => _save(lat: _lat, lng: _lng),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _ThemeModeSelector extends StatelessWidget {
  const _ThemeModeSelector();

  @override
  Widget build(BuildContext context) {
    final controller = context.watch<ThemeController>();
    final ac = context.ac;
    return SegmentedButton<ThemeMode>(
      showSelectedIcon: false,
      style: SegmentedButton.styleFrom(
        foregroundColor: ac.whiteDim,
        selectedForegroundColor: ac.ice,
        selectedBackgroundColor: ac.iceDim,
        side: BorderSide(color: ac.carbonLine),
      ),
      segments: const [
        ButtonSegment(value: ThemeMode.system, icon: Icon(Icons.brightness_auto_outlined), label: Text('Tự động')),
        ButtonSegment(value: ThemeMode.light, icon: Icon(Icons.light_mode_outlined), label: Text('Sáng')),
        ButtonSegment(value: ThemeMode.dark, icon: Icon(Icons.dark_mode_outlined), label: Text('Tối')),
      ],
      selected: {controller.mode},
      onSelectionChanged: (set) => controller.setMode(set.first),
    );
  }
}
