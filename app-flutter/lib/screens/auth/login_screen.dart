import 'package:flutter/material.dart';

import '../../services/credential_store.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/primary_button.dart';
import 'forgot_password_screen.dart';
import 'register_screen.dart';

/// Login screen: server URL (configurable + persisted, per brief) + email +
/// password. Ported UX pattern from SafeKitchen's LoginScreen — "remember
/// login" saves credentials (encrypted) for convenience, but the app always
/// re-authenticates on launch rather than silently restoring a session.
class LoginScreen extends StatefulWidget {
  const LoginScreen({
    super.key,
    required this.initialBaseUrl,
    required this.onLogin,
    required this.onRegister,
    required this.onForgot,
  });

  final String initialBaseUrl;

  /// Returns null on success, else a Vietnamese error to display.
  final Future<String?> Function(String baseUrl, String email, String password, bool remember) onLogin;

  /// Activation handler (code-only signup). This screen pushes the register
  /// screen itself so it can refresh the saved-accounts picker afterwards —
  /// a just-created account then appears here without an app restart.
  final Future<String?> Function(
    String baseUrl, String code, String email, String password, String fullName, String phone) onRegister;

  /// Forgot-password handler (e-mail a reset link).
  final Future<String?> Function(String baseUrl, String email) onForgot;

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
  List<String> _savedEmails = const [];

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
    _reloadSaved();
  }

  Future<void> _reloadSaved() async {
    final emails = await CredentialStore.listEmails();
    if (mounted) setState(() => _savedEmails = emails);
  }

  /// Show the saved accounts; picking one fills its email + password.
  Future<void> _pickAccount() async {
    if (_savedEmails.isEmpty || _busy) return;
    final chosen = await showModalBottomSheet<String>(
      context: context,
      builder: (ctx) {
        final ac = ctx.ac;
        return SafeArea(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Padding(
                padding: const EdgeInsets.fromLTRB(20, 16, 20, 8),
                child: Align(
                  alignment: Alignment.centerLeft,
                  child: Text('TÀI KHOẢN ĐÃ LƯU', style: AcText.label(size: 11, color: ac.whiteDim)),
                ),
              ),
              for (final e in _savedEmails)
                ListTile(
                  leading: Icon(Icons.person_outline, color: ac.ice),
                  title: Text(e, style: AcText.body(size: 14, color: ac.white)),
                  trailing: IconButton(
                    icon: Icon(Icons.delete_outline, color: ac.whiteDim),
                    tooltip: 'Xoá khỏi danh sách',
                    onPressed: () async {
                      await CredentialStore.removeAccount(e);
                      if (ctx.mounted) Navigator.pop(ctx);
                      _reloadSaved();
                    },
                  ),
                  onTap: () => Navigator.pop(ctx, e),
                ),
              const SizedBox(height: 8),
            ],
          ),
        );
      },
    );
    if (chosen == null) return;
    final pw = await CredentialStore.passwordFor(chosen);
    if (!mounted) return;
    setState(() {
      _email.text = chosen;
      _pass.text = pw ?? '';
      _remember = true;
    });
  }

  @override
  void dispose() {
    _url.dispose();
    _email.dispose();
    _pass.dispose();
    super.dispose();
  }

  // Push register from HERE (not the gate) so we can refresh the saved-accounts
  // picker once it pops — a just-created account then shows immediately.
  Future<void> _openRegister() async {
    await Navigator.of(context).push(MaterialPageRoute(
      builder: (_) => RegisterScreen(initialBaseUrl: _url.text.trim(), onRegister: widget.onRegister),
    ));
    await _reloadSaved();
  }

  void _openForgot() {
    Navigator.of(context).push(MaterialPageRoute(
      builder: (_) => ForgotPasswordScreen(baseUrl: _url.text.trim(), onSubmit: widget.onForgot),
    ));
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
                // Email field with a saved-accounts picker: the dropdown suffix
                // (and tapping the empty field) opens the list; picking one
                // fills its saved password.
                TextField(
                  controller: _email,
                  keyboardType: TextInputType.emailAddress,
                  enabled: !_busy,
                  onTap: (_savedEmails.isNotEmpty && _email.text.isEmpty) ? _pickAccount : null,
                  decoration: InputDecoration(
                    labelText: 'Email',
                    prefixIcon: const Icon(Icons.mail_outline),
                    suffixIcon: _savedEmails.isEmpty
                        ? null
                        : IconButton(
                            icon: const Icon(Icons.arrow_drop_down),
                            tooltip: 'Chọn tài khoản đã lưu',
                            onPressed: _busy ? null : _pickAccount,
                          ),
                  ),
                ),
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
                  onPressed: _busy ? null : _openForgot,
                  child: const Text('Quên mật khẩu?'),
                ),
                TextButton(
                  onPressed: _busy ? null : _openRegister,
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
