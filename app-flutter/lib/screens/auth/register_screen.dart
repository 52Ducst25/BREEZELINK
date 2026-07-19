import 'package:flutter/material.dart';

import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/primary_button.dart';

/// Activation screen: the only way into a new account.
///
/// Registration is closed — the vendor records the sale, creates the
/// household, and hands the buyer a code. There is deliberately no "create an
/// organization" field here: the code already names the household it belongs
/// to, and letting the app pass one would re-open the self-service signup the
/// code flow exists to close.
class RegisterScreen extends StatefulWidget {
  const RegisterScreen({super.key, required this.initialBaseUrl, required this.onRegister});

  final String initialBaseUrl;

  /// Returns null on success, else a Vietnamese error to display.
  final Future<String?> Function(
    String baseUrl,
    String code,
    String email,
    String password,
    String fullName,
    String phone,
  ) onRegister;

  @override
  State<RegisterScreen> createState() => _RegisterScreenState();
}

class _RegisterScreenState extends State<RegisterScreen> {
  late final _url = TextEditingController(text: widget.initialBaseUrl);
  final _code = TextEditingController();
  final _email = TextEditingController();
  final _pass = TextEditingController();
  final _name = TextEditingController();
  final _phone = TextEditingController();
  bool _busy = false;
  bool _success = false;
  String? _error;

  @override
  void dispose() {
    _url.dispose();
    _code.dispose();
    _email.dispose();
    _pass.dispose();
    _name.dispose();
    _phone.dispose();
    super.dispose();
  }

  String? _validate() {
    if (_code.text.trim().isEmpty) return 'Nhập mã kích hoạt được cấp khi mua máy.';
    if (_email.text.trim().isEmpty) return 'Nhập email.';
    // Mirrors the server's min_length=8 so a doomed request never leaves the
    // phone — the API would reject it with a 422 the user cannot act on.
    if (_pass.text.length < 8) return 'Mật khẩu cần ít nhất 8 ký tự.';
    return null;
  }

  Future<void> _submit() async {
    if (_busy) return;
    final invalid = _validate();
    if (invalid != null) {
      setState(() => _error = invalid);
      return;
    }
    setState(() {
      _busy = true;
      _error = null;
    });
    String? err;
    try {
      err = await widget.onRegister(
        _url.text.trim(),
        _code.text.trim(),
        _email.text.trim(),
        _pass.text,
        _name.text.trim(),
        _phone.text.trim(),
      );
    } catch (_) {
      err = 'Lỗi kết nối máy chủ. Kiểm tra địa chỉ và mạng rồi thử lại.';
    }
    if (!mounted) return;
    if (err == null) {
      // Success: show the confirmation, then return to login on its own so the
      // customer signs in deliberately with the account they just created.
      setState(() {
        _busy = false;
        _success = true;
      });
      Future.delayed(const Duration(milliseconds: 1800), () {
        if (mounted) Navigator.of(context).maybePop();
      });
    } else {
      setState(() {
        _busy = false;
        _error = err;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    if (_success) {
      return Scaffold(
        appBar: AppBar(title: const Text('Kích hoạt tài khoản')),
        body: Center(
          child: Padding(
            padding: const EdgeInsets.all(24),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(Icons.check_circle_outline, size: 64, color: ac.success),
                const SizedBox(height: 16),
                Text('Tạo tài khoản thành công!',
                    textAlign: TextAlign.center, style: AcText.heading(size: 18, color: ac.white)),
                const SizedBox(height: 8),
                Text('Đang quay về màn hình đăng nhập…',
                    textAlign: TextAlign.center, style: AcText.body(size: 13, color: ac.whiteDim)),
              ],
            ),
          ),
        ),
      );
    }
    return Scaffold(
      appBar: AppBar(title: const Text('Kích hoạt tài khoản')),
      body: Center(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(24),
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 400),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Icon(Icons.vpn_key_outlined, size: 48, color: ac.ice),
                const SizedBox(height: 12),
                Text(
                  'Nhập mã kích hoạt được cấp khi mua máy để tạo tài khoản.',
                  textAlign: TextAlign.center,
                  style: AcText.body(size: 12.5, color: ac.whiteDim),
                ),
                const SizedBox(height: 20),
                _field(_url, 'Địa chỉ máy chủ', Icons.dns_outlined),
                const SizedBox(height: 12),
                // Digits only: the codes the vendor issues are 8 numerals
                // (services/invite_service._generate), so a number pad is the
                // right keyboard and letters are never valid input.
                _field(_code, 'Mã kích hoạt', Icons.confirmation_number_outlined,
                    keyboard: TextInputType.number),
                const SizedBox(height: 12),
                _field(_email, 'Email', Icons.mail_outline, keyboard: TextInputType.emailAddress),
                const SizedBox(height: 12),
                _field(_pass, 'Mật khẩu (ít nhất 8 ký tự)', Icons.lock_outline, obscure: true),
                const SizedBox(height: 12),
                _field(_name, 'Họ tên (không bắt buộc)', Icons.person_outline),
                const SizedBox(height: 12),
                _field(_phone, 'Số điện thoại (không bắt buộc)', Icons.phone_outlined,
                    keyboard: TextInputType.phone),
                if (_error != null) ...[
                  const SizedBox(height: 12),
                  Text(_error!, style: AcText.body(size: 12.5, color: ac.error)),
                ],
                const SizedBox(height: 20),
                _busy
                    ? const Center(child: CircularProgressIndicator())
                    : PrimaryButton(
                        label: 'Tạo tài khoản',
                        icon: Icons.check,
                        onPressed: _submit,
                      ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _field(TextEditingController c, String label, IconData icon,
      {bool obscure = false, TextInputType? keyboard}) {
    return TextField(
      controller: c,
      obscureText: obscure,
      keyboardType: keyboard,
      enabled: !_busy,
      decoration: InputDecoration(labelText: label, prefixIcon: Icon(icon)),
    );
  }
}
