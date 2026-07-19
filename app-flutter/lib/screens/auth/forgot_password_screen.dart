import 'package:flutter/material.dart';

import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/primary_button.dart';

/// "Quên mật khẩu" — enter the account e-mail; the server e-mails a reset link
/// (which opens the web reset page). Deliberately shows the SAME confirmation
/// whether or not the e-mail exists — the server never reveals which addresses
/// are registered, and neither should this screen.
class ForgotPasswordScreen extends StatefulWidget {
  const ForgotPasswordScreen({super.key, required this.baseUrl, required this.onSubmit});

  final String baseUrl;

  /// Returns null on success (request accepted), else a Vietnamese error.
  final Future<String?> Function(String baseUrl, String email) onSubmit;

  @override
  State<ForgotPasswordScreen> createState() => _ForgotPasswordScreenState();
}

class _ForgotPasswordScreenState extends State<ForgotPasswordScreen> {
  final _email = TextEditingController();
  bool _busy = false;
  bool _sent = false;
  String? _error;

  @override
  void dispose() {
    _email.dispose();
    super.dispose();
  }

  Future<void> _submit() async {
    if (_busy) return;
    if (_email.text.trim().isEmpty) {
      setState(() => _error = 'Nhập email tài khoản.');
      return;
    }
    setState(() {
      _busy = true;
      _error = null;
    });
    String? err;
    try {
      err = await widget.onSubmit(widget.baseUrl, _email.text.trim());
    } catch (_) {
      err = 'Lỗi kết nối máy chủ. Kiểm tra mạng rồi thử lại.';
    }
    if (!mounted) return;
    setState(() {
      _busy = false;
      _error = err;
      _sent = err == null;
    });
  }

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return Scaffold(
      appBar: AppBar(title: const Text('Quên mật khẩu')),
      body: Center(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(24),
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 400),
            child: _sent
                ? Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(Icons.mark_email_read_outlined, size: 56, color: ac.ice),
                      const SizedBox(height: 16),
                      Text(
                        'Đã gửi liên kết đặt lại',
                        textAlign: TextAlign.center,
                        style: AcText.heading(size: 18, color: ac.white),
                      ),
                      const SizedBox(height: 8),
                      Text(
                        'Nếu email này có tài khoản, một liên kết đặt lại mật khẩu đã được '
                        'gửi tới hộp thư. Mở liên kết để đặt mật khẩu mới (hết hạn sau 30 phút).',
                        textAlign: TextAlign.center,
                        style: AcText.body(size: 13, color: ac.whiteDim),
                      ),
                      const SizedBox(height: 24),
                      PrimaryButton(
                        label: 'Về đăng nhập',
                        icon: Icons.arrow_back,
                        onPressed: () => Navigator.of(context).pop(),
                      ),
                    ],
                  )
                : Column(
                    mainAxisSize: MainAxisSize.min,
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      Icon(Icons.lock_reset_outlined, size: 48, color: ac.ice),
                      const SizedBox(height: 12),
                      Text(
                        'Nhập email tài khoản. Chúng tôi sẽ gửi liên kết để bạn đặt mật khẩu mới.',
                        textAlign: TextAlign.center,
                        style: AcText.body(size: 12.5, color: ac.whiteDim),
                      ),
                      const SizedBox(height: 20),
                      TextField(
                        controller: _email,
                        enabled: !_busy,
                        keyboardType: TextInputType.emailAddress,
                        decoration: const InputDecoration(
                          labelText: 'Email',
                          prefixIcon: Icon(Icons.mail_outline),
                        ),
                      ),
                      if (_error != null) ...[
                        const SizedBox(height: 12),
                        Text(_error!, style: AcText.body(size: 12.5, color: ac.error)),
                      ],
                      const SizedBox(height: 20),
                      _busy
                          ? const Center(child: CircularProgressIndicator())
                          : PrimaryButton(
                              label: 'Gửi liên kết',
                              icon: Icons.send,
                              onPressed: _submit,
                            ),
                    ],
                  ),
          ),
        ),
      ),
    );
  }
}
