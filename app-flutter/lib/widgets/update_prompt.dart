import 'package:dio/dio.dart';
import 'package:flutter/material.dart';
import 'package:open_file/open_file.dart';
import 'package:path_provider/path_provider.dart';

import '../services/ota_service.dart';

/// Launch-time check: prompt ONLY when there is actually a newer build. Silent
/// on "up to date" and on network failure — a launch check must never interrupt
/// someone who just opened the app.
Future<void> maybePromptUpdate(BuildContext context, String baseUrl) async {
  final ota = OtaService(baseUrl);
  final check = await ota.check();
  if (check == null || !check.available) return;
  if (!context.mounted) return;
  await _showUpdateDialog(context, ota, check);
}

/// Manual check from Settings: ALWAYS gives feedback — the update dialog if a
/// newer build exists, otherwise a "you're up to date" / "couldn't check"
/// snackbar.
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

/// The shared "a new version is available" dialog.
Future<void> _showUpdateDialog(BuildContext context, OtaService ota, UpdateCheck check) async {
  final info = check.info;
  final sizeMb = info.apkSize > 0 ? (info.apkSize / 1048576).toStringAsFixed(1) : null;
  await showDialog<void>(
    context: context,
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
              if (!check.forced) Navigator.of(ctx).pop();
              await _downloadAndInstall(context, ota, info);
            },
            child: const Text('Cập nhật ngay'),
          ),
        ],
      ),
    ),
  );
}

/// Download the APK inside the app (with a progress bar) and hand it to the
/// system installer — no bouncing out to a browser. Android 8+ shows its own
/// "install unknown apps" prompt the first time; that OS gate is expected.
Future<void> _downloadAndInstall(BuildContext context, OtaService ota, AppUpdateInfo info) async {
  final progress = ValueNotifier<double>(0);
  // Hold the DIALOG's own context so we can always dismiss THAT route, even if
  // the host subtree tore down mid-download (a background 401 → force logout).
  BuildContext? dialogCtx;
  void closeDialog() {
    final c = dialogCtx;
    if (c != null && c.mounted) Navigator.of(c).pop();
    dialogCtx = null;
  }

  try {
    final dir = await getTemporaryDirectory();
    final path = '${dir.path}/aircon-${info.latestVersionCode}.apk';
    if (!context.mounted) return;

    showDialog<void>(
      context: context,
      barrierDismissible: false,
      builder: (ctx) {
        dialogCtx = ctx;
        return PopScope(
          canPop: false,
          child: AlertDialog(
            title: const Text('Đang tải bản cập nhật'),
            content: ValueListenableBuilder<double>(
              valueListenable: progress,
              builder: (_, p, _) => Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  LinearProgressIndicator(value: p > 0 ? p : null),
                  const SizedBox(height: 12),
                  Text(p > 0 ? '${(p * 100).toStringAsFixed(0)}%' : 'Bắt đầu tải…'),
                ],
              ),
            ),
          ),
        );
      },
    );

    await Dio().download(
      ota.downloadUrl(info),
      path,
      onReceiveProgress: (received, total) {
        if (total > 0) progress.value = received / total;
      },
    );
    closeDialog();

    // Hand the file to Android's package installer.
    final result = await OpenFile.open(path, type: 'application/vnd.android.package-archive');
    if (result.type != ResultType.done && context.mounted) {
      _snack(context,
          'Không mở được trình cài đặt. Vào Cài đặt → cho phép Aircon "Cài ứng dụng không rõ nguồn gốc" rồi thử lại.');
    }
  } catch (_) {
    closeDialog();
    if (context.mounted) _snack(context, 'Tải bản cập nhật thất bại. Kiểm tra mạng rồi thử lại.');
  } finally {
    // Defer disposal until after the current frame so a still-mounted
    // ValueListenableBuilder for the dialog isn't reading a disposed notifier.
    WidgetsBinding.instance.addPostFrameCallback((_) => progress.dispose());
  }
}
