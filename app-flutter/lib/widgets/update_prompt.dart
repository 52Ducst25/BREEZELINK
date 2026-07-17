import 'package:flutter/material.dart';
import 'package:url_launcher/url_launcher.dart';

import '../services/ota_service.dart';

/// Shows the "a new version is available" dialog, if there is one.
///
/// Install strategy: hand the APK URL to the browser rather than downloading
/// it in-process. Doing it in-app would mean the REQUEST_INSTALL_PACKAGES
/// permission, a FileProvider entry, and a runtime permission prompt — a lot
/// of surface for a flow the browser already does natively (download, then
/// tap to install). If that ever becomes too clunky, the download endpoint is
/// unchanged; only this widget swaps.
Future<void> maybePromptUpdate(BuildContext context, String baseUrl) async {
  final ota = OtaService(baseUrl);
  final check = await ota.check();
  // check() swallows network failures and returns null — a launch-time check
  // must never interrupt someone who just wants to use the app.
  if (check == null || !check.available) return;
  if (!context.mounted) return;

  final info = check.info;
  final sizeMb = info.apkSize > 0 ? (info.apkSize / 1048576).toStringAsFixed(1) : null;

  await showDialog<void>(
    context: context,
    // A forced update is not dismissible: this build is below the server's
    // floor and is not supposed to keep running.
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
              // externalApplication = hand off to the browser, which downloads
              // the file and offers to install it.
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
