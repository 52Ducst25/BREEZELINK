import 'package:flutter/material.dart';

import '../../services/credential_store.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/primary_button.dart';

/// Login screen: server URL (configurable + persisted, per brief) + email +
/// password. Ported UX pattern from SafeKitchen's LoginScreen — "remember
/// login" saves credentials (encrypted) for convenience, but the app always
/// re-authenticates on launch rather than silently restoring a session.
class LoginScreen extends StatefulWidget {
  const LoginScreen({
    super.key,
    required this.initialBaseUrl,
    required this.onLogin,
    required this.onRegisterTap,
  });

  final String initialBaseUrl;

  /// Returns null on success, else a Vietnamese error to display.
  final Future<String?> Function(String baseUrl, String email, String password, bool remember) onLogin;

  /// Opens the activation screen. Registration is code-only, so this is the
  /// single entry point for a customer who has just bought a unit.
  final VoidCallback onRegisterTap;

  @override
  State<LoginScreen> createState() => _LoginScreenState();
}

class _LoginScreenState extends State<LoginScreen> {
  late final _url = TextEditingController(text: widget.initialBaseUrl);
  final _email = TextEditingController();
  final _pass = TextEditingController();
  bool _busy = false;
  bool _remember = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    CredentialStore.load().then((saved) {
      if (!mounted || !saved.remember) return;
      setState(() {
        _email.text = saved.email;
        _pass.text = saved.password;
        _remember = true;
      });
    });
  }

  @override
  void dispose() {
    _url.dispose();
    _email.dispose();
    _pass.dispose();
    super.dispose();
  }

  Future<void> _submit() async {
    if (_busy) return;
    setState(() {
      _busy = true;
      _error = null;
    });
    String? err;
    try {
      err = await widget.onLogin(_url.text.trim(), _email.text.trim(), _pass.text, _remember);
    } catch (_) {
      err = 'Lỗi kết nối máy chủ. Kiểm tra địa chỉ và mạng rồi thử lại.';
    } finally {
      if (mounted) {
        setState(() {
          _busy = false;
          _error = err;
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return Scaffold(
      body: Center(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(24),
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 400),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Icon(Icons.ac_unit, size: 56, color: ac.ice),
                const SizedBox(height: 8),
                Text('AIRCON', textAlign: TextAlign.center, style: AcText.heading(size: 22, color: ac.white)),
                const SizedBox(height: 4),
                Text('Điều hòa thích ứng', textAlign: TextAlign.center, style: AcText.body(size: 12, color: ac.whiteDim)),
                const SizedBox(height: 24),
                _field(_url, 'Địa chỉ máy chủ', Icons.dns_outlined),
                const SizedBox(height: 12),
                _field(_email, 'Email', Icons.mail_outline, keyboard: TextInputType.emailAddress),
                const SizedBox(height: 12),
                _field(_pass, 'Mật khẩu', Icons.lock_outline, obscure: true),
                const SizedBox(height: 4),
                CheckboxListTile(
                  value: _remember,
                  onChanged: _busy ? null : (v) => setState(() => _remember = v ?? false),
                  title: const Text('Ghi nhớ đăng nhập'),
                  controlAffinity: ListTileControlAffinity.leading,
                  contentPadding: EdgeInsets.zero,
                  dense: true,
                ),
                if (_error != null) ...[
                  const SizedBox(height: 12),
                  Text(_error!, style: AcText.body(size: 12.5, color: ac.error)),
                ],
                const SizedBox(height: 20),
                _busy
                    ? const Center(child: CircularProgressIndicator())
                    : PrimaryButton(label: 'Đăng nhập', icon: Icons.login, onPressed: _submit),
                const SizedBox(height: 8),
                TextButton(
                  onPressed: _busy ? null : widget.onRegisterTap,
                  child: const Text('Mới mua máy? Kích hoạt bằng mã'),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _field(TextEditingController c, String label, IconData icon, {bool obscure = false, TextInputType? keyboard}) {
    return TextField(
      controller: c,
      obscureText: obscure,
      keyboardType: keyboard,
      enabled: !_busy,
      decoration: InputDecoration(labelText: label, prefixIcon: Icon(icon)),
    );
  }
}
