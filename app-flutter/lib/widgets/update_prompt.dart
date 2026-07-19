import 'package:flutter/material.dart';
import 'package:url_launcher/url_launcher.dart';

import '../services/ota_service.dart';

/// Launch-time check: prompt ONLY when there is actually a newer build. Silent
/// on "up to date" and on network failure — a launch check must never
/// interrupt or nag someone who just opened the app.
Future<void> maybePromptUpdate(BuildContext context, String baseUrl) async {
  final ota = OtaService(baseUrl);
  final check = await ota.check();
  if (check == null || !check.available) return;
  if (!context.mounted) return;
  await _showUpdateDialog(context, ota, check);
}

/// Manual check from Settings: ALWAYS gives feedback — the update dialog if a
/// newer build exists, otherwise a "you're up to date" / "couldn't check"
/// snackbar. That confirmation is the whole point of a manual "check" button.
Future<void> checkForUpdate(BuildContext context, String baseUrl) async {
  showDialog<void>(
    context: context,
    barrierDismissible: false,
    builder: (_) => const Center(child: CircularProgressIndicator()),
  );
  final check = await OtaService(baseUrl).check();
  if (!context.mounted) return;
  Navigator.of(context).pop(); // dismiss the spinner
  if (!context.mounted) return;

  if (check == null) {
    _snack(context, 'Không kiểm tra được — kiểm tra mạng rồi thử lại.');
    return;
  }
  if (!check.available) {
    _snack(context, 'Bạn đang dùng phiên bản mới nhất.');
    return;
  }
  await _showUpdateDialog(context, OtaService(baseUrl), check);
}

void _snack(BuildContext context, String msg) {
  ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
}

/// The shared "a new version is available" dialog. Install strategy: hand the
/// APK URL to the browser (download + tap-to-install) rather than the in-app
/// installer, which would need REQUEST_INSTALL_PACKAGES + a FileProvider.
Future<void> _showUpdateDialog(BuildContext context, OtaService ota, UpdateCheck check) async {
  final info = check.info;
  final sizeMb = info.apkSize > 0 ? (info.apkSize / 1048576).toStringAsFixed(1) : null;
  await showDialog<void>(
    context: context,
    // A forced update is not dismissible: this build is below the server's floor.
    barrierDismissible: !check.forced,
    builder: (ctx) => PopScope(
      canPop: !check.forced,
      child: AlertDialog(
        title: Text(check.forced ? 'Bắt buộc cập nhật' : 'Có bản cập nhật mới'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Phiên bản ${info.latestVersionName}'
              '${sizeMb != null ? ' · $sizeMb MB' : ''}',
              style: Theme.of(ctx).textTheme.titleSmall,
            ),
            if (info.changelog.isNotEmpty) ...[
              const SizedBox(height: 12),
              Text(info.changelog),
            ],
            if (check.forced) ...[
              const SizedBox(height: 12),
              Text(
                'Phiên bản bạn đang dùng không còn được hỗ trợ. Hãy cập nhật để tiếp tục.',
                style: TextStyle(color: Theme.of(ctx).colorScheme.error),
              ),
            ],
          ],
        ),
        actions: [
          if (!check.forced)
            TextButton(
              onPressed: () => Navigator.of(ctx).pop(),
              child: const Text('Để sau'),
            ),
          FilledButton(
            onPressed: () async {
              final uri = Uri.parse(ota.downloadUrl(info));
              await launchUrl(uri, mode: LaunchMode.externalApplication);
              if (ctx.mounted && !check.forced) Navigator.of(ctx).pop();
            },
            child: const Text('Tải bản mới'),
          ),
        ],
      ),
    ),
  );
}
