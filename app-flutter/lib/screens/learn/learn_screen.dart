import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../models/ac_action.dart';
import '../../models/ac_mode.dart';
import '../../models/ir_code.dart';
import '../../services/ir_api.dart';
import '../../state/app_state.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/icon_badge.dart';
import '../../widgets/outline_panel.dart';
import '../../widgets/primary_button.dart';
import '../../widgets/section_label.dart';

/// HỌC LỆNH tab: teach the indoor node every remote button.
///
/// Two kinds of button:
///  - (mode, temp) codes — COOL 16-30, plus one each for HÚT ẨM / QUẠT / TẮT.
///  - standalone action codes ([kAcActions]) — the peripheral remote keys
///    (tốc độ quạt, ngủ, eco, …). Each is one learned IR frame.
///
/// The flow is fire-and-forget: press "Bắt đầu học", point the real remote at
/// the node and press its button; the capture arrives over MQTT, then "Làm mới"
/// re-reads coverage so the ✓ appears.
class LearnScreen extends StatefulWidget {
  const LearnScreen({super.key});

  @override
  State<LearnScreen> createState() => _LearnScreenState();
}

class _LearnScreenState extends State<LearnScreen> {
  AcMode _mode = AcMode.cool;
  int _temp = 24;
  IrCoverage? _coverage;
  bool _busy = false;
  String? _status;

  IrApi get _irApi => context.read<AppState>().irApi;

  @override
  void initState() {
    super.initState();
    _refresh();
  }

  Future<void> _refresh() async {
    try {
      final c = await _irApi.coverage();
      if (mounted) setState(() => _coverage = c);
    } catch (_) {
      // Non-fatal: the lists just stay as they were.
    }
  }

  Future<void> _run(String label, Future<void> Function() action) async {
    setState(() {
      _busy = true;
      _status = null;
    });
    try {
      await action();
      if (mounted) {
        setState(() {
          _status = 'Đã bật chế độ HỌC cho "$label". Bấm nút tương ứng trên điều khiển '
              'thật (chĩa vào dàn lạnh) trong vài giây, rồi bấm "Làm mới".';
        });
      }
    } catch (e) {
      if (mounted) setState(() => _status = 'Lỗi: $e');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  /// Xoá một mã đã học, có hỏi lại. Học nhầm nút trên remote là chuyện thường,
  /// mà học lại đè lên thì vẫn còn mã cũ nếu người dùng đổi ý — nên cần đường
  /// xoá hẳn để nút đó quay về "chưa học".
  Future<void> _confirmDelete(String label, Future<void> Function() delete) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Xoá mã đã học'),
        content: Text('Xoá mã đã học của "$label"? Nút này sẽ quay về trạng thái '
            'chưa học, và bạn có thể học lại từ đầu.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Hủy')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Xoá')),
        ],
      ),
    );
    if (ok != true) return;

    setState(() {
      _busy = true;
      _status = null;
    });
    try {
      await delete();
      // Đọc lại phủ sóng NGAY thay vi bat nguoi dung bam "Lam moi": xoá là thao
      // tác đồng bộ, kết quả có liền, khác hẳn học (bất đồng bộ qua MQTT).
      final c = await _irApi.coverage();
      if (mounted) {
        setState(() {
          _coverage = c;
          _status = 'Đã xoá mã "$label".';
        });
      }
    } catch (e) {
      if (mounted) setState(() => _status = 'Lỗi khi xoá: $e');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Text('Học lệnh hồng ngoại', style: AcText.heading(size: 18, color: ac.white)),
        const SizedBox(height: 6),
        Text(
          'Dạy cho thiết bị từng nút trên điều khiển thật. Học một lần, sau đó tab '
          'Điều khiển dùng lại mã đã học để bấm nút đó.',
          style: AcText.body(size: 12.5, color: ac.whiteDim),
        ),
        const SizedBox(height: 16),

        // ---- Mode + temperature codes ----
        const SectionLabel('Chế độ & nhiệt độ'),
        const SizedBox(height: 10),
        OutlinePanel(child: _modeTempLearner(ac)),
        const SizedBox(height: 20),

        // ---- Peripheral action buttons ----
        const SectionLabel('Nút chức năng'),
        const SizedBox(height: 10),
        OutlinePanel(
          child: Column(
            children: [
              for (final a in kAcActions) ...[
                _actionRow(ac, a),
                if (a != kAcActions.last) Divider(color: ac.carbonLine, height: 18),
              ],
            ],
          ),
        ),

        if (_status != null) ...[
          const SizedBox(height: 14),
          OutlinePanel(
            accent: ac.ice,
            child: Text(_status!, style: AcText.body(size: 12, color: ac.white)),
          ),
        ],
        const SizedBox(height: 24),
        Center(
          child: PrimaryButton(label: 'Làm mới trạng thái', icon: Icons.refresh, primary: false, onPressed: _refresh),
        ),
      ],
    );
  }

  Widget _modeTempLearner(AcPalette ac) {
    final coverage = _coverage;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Wrap(
          spacing: 8,
          children: [
            for (final m in AcMode.values)
              ChoiceChip(
                label: Text(m.label, style: AcText.label(size: 11)),
                selected: _mode == m,
                onSelected: _busy ? null : (_) => setState(() => _mode = m),
              ),
          ],
        ),
        if (_mode == AcMode.cool) ...[
          const SizedBox(height: 10),
          Row(
            children: [
              Text('Nhiệt độ', style: AcText.body(size: 12, color: ac.whiteDim)),
              const Spacer(),
              IconButton(onPressed: _busy ? null : () => setState(() => _temp--), icon: const Icon(Icons.remove, size: 18)),
              Text('$_temp°C', style: AcText.mono(size: 14, color: ac.white)),
              IconButton(onPressed: _busy ? null : () => setState(() => _temp++), icon: const Icon(Icons.add, size: 18)),
            ],
          ),
        ],
        const SizedBox(height: 12),
        PrimaryButton(
          label: 'Bắt đầu học',
          icon: Icons.settings_remote,
          onPressed: _busy
              ? null
              : () => _run(
                    _mode == AcMode.cool ? 'Lạnh $_temp°C' : _mode.label,
                    () => _irApi.triggerLearn(mode: _mode, temp: _mode == AcMode.cool ? _temp : null),
                  ),
        ),
        const SizedBox(height: 12),
        Divider(color: ac.carbonLine),
        const SizedBox(height: 8),
        if (coverage == null)
          Text('Đang tải danh sách mã đã học…', style: AcText.body(size: 12, color: ac.whiteDim))
        else ...[
          Text(
            coverage.isComplete
                ? 'Đã đủ mã lệnh cơ bản — tự động điều khiển sẵn sàng.'
                : 'Còn thiếu: ${coverage.missing.join(', ')}',
            style: AcText.body(size: 12, color: coverage.isComplete ? ac.success : ac.warning),
          ),
          const SizedBox(height: 8),
          if (coverage.codes.isNotEmpty)
            Text('Bấm dấu × trên mã để xoá nếu học nhầm nút.',
                style: AcText.body(size: 11, color: ac.whiteDim)),
          const SizedBox(height: 6),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (final code in coverage.codes)
                Chip(
                  visualDensity: VisualDensity.compact,
                  avatar: Icon(Icons.check, size: 14, color: ac.success),
                  label: Text(code.buttonLabel, style: AcText.mono(size: 11, color: ac.white)),
                  // deleteIcon + onDeleted là đường xoá dựng sẵn của Chip: hiện
                  // dấu × ngay trên nhãn, đúng chỗ mắt người dùng đang nhìn.
                  deleteIcon: const Icon(Icons.close, size: 15),
                  deleteButtonTooltipMessage: 'Xoá mã ${code.buttonLabel}',
                  onDeleted: _busy
                      ? null
                      : () => _confirmDelete(
                            code.buttonLabel,
                            () => _irApi.deleteCode(code.id),
                          ),
                ),
            ],
          ),
        ],
      ],
    );
  }

  Widget _actionRow(AcPalette ac, AcAction a) {
    final learned = _coverage?.actions.contains(a.wire) ?? false;
    return Row(
      children: [
        AcIconBadge(icon: a.icon, size: 40, iconSize: 20, color: learned ? ac.ice : ac.whiteDim),
        const SizedBox(width: 12),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(a.label, style: AcText.body(size: 13, color: ac.white)),
              Row(
                children: [
                  Icon(learned ? Icons.check_circle : Icons.remove_circle_outline,
                      size: 12, color: learned ? ac.success : ac.whiteDim),
                  const SizedBox(width: 4),
                  Text(learned ? 'đã học' : 'chưa học',
                      style: AcText.label(size: 10, color: learned ? ac.success : ac.whiteDim)),
                ],
              ),
            ],
          ),
        ),
        PrimaryButton(
          label: learned ? 'Học lại' : 'Học',
          primary: !learned,
          onPressed: _busy ? null : () => _run(a.label, () => _irApi.triggerLearnAction(a.wire)),
        ),
        // Nút xoá chỉ hiện khi đã học — chưa học thì không có gì để xoá, mà
        // một nút luôn hiện rồi báo lỗi khi bấm là thiết kế tệ.
        if (learned) ...[
          const SizedBox(width: 4),
          IconButton(
            onPressed: _busy
                ? null
                : () => _confirmDelete(a.label, () => _irApi.deleteAction(a.wire)),
            icon: const Icon(Icons.delete_outline, size: 20),
            color: ac.whiteDim,
            tooltip: 'Xoá mã đã học',
            visualDensity: VisualDensity.compact,
          ),
        ],
      ],
    );
  }
}
