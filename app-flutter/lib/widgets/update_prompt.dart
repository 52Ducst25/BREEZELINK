import 'package:dio/dio.dart';
import 'package:flutter/material.dart';
import 'package:open_file/open_file.dart';
import 'package:path_provider/path_provider.dart';
import 'package:provider/provider.dart';

import '../models/app_notification.dart';
import '../services/notification_store.dart';
import '../services/ota_service.dart';

/// Ghi bản cập nhật vừa tìm thấy vào lịch sử thông báo (nút chuông).
///
/// Khoá theo versionCode nên mở app mười lần cũng chỉ có MỘT dòng cho mỗi bản —
/// xem chú thích ở [AppNotification.id]. Ghi TRƯỚC khi hiện hộp thoại, để người
/// bấm "Để sau" vẫn còn dấu vết tìm lại được.
Future<void> _recordUpdateNotification(BuildContext context, UpdateCheck check) async {
  final info = check.info;
  await context.read<NotificationStore>().add(AppNotification(
        id: 'update:${info.latestVersionCode}',
        kind: 'update',
        title: check.forced
            ? 'Bắt buộc cập nhật ${info.latestVersionName}'
            : 'Có bản cập nhật ${info.latestVersionName}',
        body: info.changelog,
        ts: DateTime.now(),
        read: false,
      ));
}

/// Launch-time check: prompt ONLY when there is actually a newer build. Silent
/// on "up to date" and on network failure — a launch check must never interrupt
/// someone who just opened the app.
Future<void> maybePromptUpdate(BuildContext context, String baseUrl) async {
  final ota = OtaService(baseUrl);
  final check = await ota.check();
  if (check == null || !check.available) return;
  if (!context.mounted) return;
  await _recordUpdateNotification(context, check);
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
  await _recordUpdateNotification(context, check);
  if (!context.mounted) return;
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
/// Tiến độ tải: phần trăm + số byte, để người dùng phân biệt "đang chậm" với
/// "đã đứng hình" — chỉ nhìn phần trăm thì hai thứ đó giống hệt nhau.
typedef _DlProgress = ({double pct, int received, int total});

String _mb(int bytes) => '${(bytes / 1048576).toStringAsFixed(1)} MB';

Future<void> _downloadAndInstall(BuildContext context, OtaService ota, AppUpdateInfo info) async {
  final progress = ValueNotifier<_DlProgress>((pct: 0, received: 0, total: 0));
  final cancelToken = CancelToken();
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
    final path = '${dir.path}/breezelink-${info.latestVersionCode}.apk';
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
            content: ValueListenableBuilder<_DlProgress>(
              valueListenable: progress,
              builder: (_, p, _) => Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  LinearProgressIndicator(value: p.pct > 0 ? p.pct : null),
                  const SizedBox(height: 12),
                  Text(p.pct > 0
                      ? '${(p.pct * 100).toStringAsFixed(0)}%  ·  ${_mb(p.received)} / ${_mb(p.total)}'
                      : 'Bắt đầu tải…'),
                ],
              ),
            ),
            // Không có nút này thì một kết nối chết lặng giữa chừng sẽ nhốt
            // người dùng trong hộp thoại không đóng được, phải tắt hẳn app.
            actions: [
              TextButton(
                onPressed: () => cancelToken.cancel('nguoi dung huy'),
                child: const Text('Huỷ'),
              ),
            ],
          ),
        );
      },
    );

    // Timeout là thứ QUYẾT ĐỊNH ở đây: Dio mặc định KHÔNG có receiveTimeout, nên
    // khi kết nối di động chết lặng giữa chừng (đổi WiFi/4G, sóng yếu) lệnh tải
    // treo vĩnh viễn — thanh tiến trình đứng im ở giữa chừng, không lỗi, không
    // thoát được. receiveTimeout đo KHOẢNG LẶNG GIỮA HAI GÓI DỮ LIỆU, không phải
    // tổng thời gian tải, nên file 56 MB tải chậm vẫn không bị cắt oan.
    final dio = Dio(BaseOptions(
      connectTimeout: const Duration(seconds: 20),
      receiveTimeout: const Duration(seconds: 45),
    ));

    // Mạng di động chập chờn thường hỏng ở lần đầu rồi lại được — thử lại vài
    // lần trước khi bắt người dùng tự bấm lại từ đầu.
    const maxAttempts = 3;
    for (var attempt = 1; ; attempt++) {
      try {
        await dio.download(
          ota.downloadUrl(info),
          path,
          cancelToken: cancelToken,
          onReceiveProgress: (received, total) {
            if (total > 0) {
              progress.value = (pct: received / total, received: received, total: total);
            }
          },
        );
        break; // xong
      } on DioException catch (e) {
        // Người dùng bấm Huỷ, hoặc đã thử đủ số lần -> để catch ngoài xử lý.
        if (CancelToken.isCancel(e) || attempt >= maxAttempts) rethrow;
        progress.value = (pct: 0, received: 0, total: 0); // tải lại từ đầu
      }
    }
    closeDialog();

    // Hand the file to Android's package installer.
    final result = await OpenFile.open(path, type: 'application/vnd.android.package-archive');
    if (result.type != ResultType.done && context.mounted) {
      _snack(context,
          'Không mở được trình cài đặt. Vào Cài đặt → cho phép BreezeLink "Cài ứng dụng không rõ nguồn gốc" rồi thử lại.');
    }
  } on DioException catch (e) {
    closeDialog();
    if (!context.mounted) return;
    // Gộp mọi lỗi vào một câu chung khiến người dùng không biết nên làm gì —
    // "đã huỷ" và "mạng đứt giữa chừng" cần hai hành động khác hẳn nhau.
    if (CancelToken.isCancel(e)) {
      _snack(context, 'Đã huỷ tải bản cập nhật.');
    } else if (e.response?.statusCode == 429) {
      // Máy chủ giới hạn số lượt tải/giờ cho mỗi IP. Nói thẳng ra, nếu không
      // người dùng sẽ hiểu nhầm là mất mạng rồi bấm lại liên tục càng kẹt thêm.
      _snack(context, 'Đã tải lại quá nhiều lần trong một giờ. Chờ khoảng 10 phút rồi thử lại.');
    } else if (e.type == DioExceptionType.receiveTimeout ||
        e.type == DioExceptionType.connectionTimeout ||
        e.type == DioExceptionType.connectionError) {
      final got = progress.value.received;
      _snack(context,
          'Mạng đứt giữa chừng khi tải${got > 0 ? ' (được ${_mb(got)})' : ''}. '
          'Nên dùng WiFi ổn định rồi thử lại — bản cập nhật nặng ~54 MB.');
    } else {
      _snack(context, 'Tải bản cập nhật thất bại. Kiểm tra mạng rồi thử lại.');
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
