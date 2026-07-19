import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../services/api_client.dart';
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
              _row(ac, 'Email', profile?.email ?? '—'),
              _row(ac, 'Vai trò', profile == null ? '—' : (profile.isOwner ? 'Chủ sở hữu' : 'Thành viên')),
              _row(ac, 'Máy chủ', s.apiClient.baseUrl),
            ],
          ),
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
              _row(ac, 'Phiên bản', ApiClient.appVersion.isEmpty ? '—' : ApiClient.appVersion),
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

  Widget _row(AcPalette ac, String label, String value) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 6),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(label, style: AcText.body(size: 12.5, color: ac.whiteDim)),
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
