import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../screens/account/account_screen.dart';
import '../screens/control/control_screen.dart';
import '../screens/dashboard/dashboard_screen.dart';
import '../screens/learn/learn_screen.dart';
import '../state/app_state.dart';
import '../theme/ac_colors.dart';
import '../theme/ac_text.dart';
import '../widgets/notification_bell.dart';

/// Main navigation shell: three tabs with ĐIỀU KHIỂN in the MIDDLE (the remote
/// is the app's primary surface), flanked by TRẠNG THÁI and HỌC LỆNH. Account
/// is no longer a tab — it lives behind the gear in the top-right corner.
///
/// M3 [NavigationBar] over an [IndexedStack] so each tab keeps its state
/// (scroll position, in-flight learn) when switching.
class MainShell extends StatefulWidget {
  const MainShell({super.key, required this.onLogout});

  final VoidCallback onLogout;

  @override
  State<MainShell> createState() => _MainShellState();
}

class _MainShellState extends State<MainShell> {
  // Start on the remote (middle tab) — it is what the customer opens the app for.
  int _index = 1;

  static const _titles = ['Trạng thái', 'Điều khiển', 'Học lệnh'];

  void _openAccount() {
    // AccountScreen reads AppState via context.watch. This route is pushed on
    // the ROOT navigator, which sits ABOVE the ChangeNotifierProvider<AppState>
    // in auth_gate (it wraps only MainShell), so the pushed subtree is NOT a
    // descendant of that provider — reading it there throws
    // ProviderNotFoundException. Re-provide the SAME instance into the route.
    final appState = context.read<AppState>();
    Navigator.of(context).push(MaterialPageRoute(
      builder: (_) => ChangeNotifierProvider<AppState>.value(
        value: appState,
        child: Scaffold(
          appBar: AppBar(title: const Text('Tài khoản')),
          body: SafeArea(child: AccountScreen(onLogout: widget.onLogout)),
        ),
      ),
    ));
  }

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final screens = const [DashboardScreen(), ControlScreen(), LearnScreen()];

    return Scaffold(
      appBar: AppBar(
        titleSpacing: 16,
        title: Row(
          children: [
            Text('BREEZE', style: AcText.heading(size: 17, color: ac.white)),
            Text('LINK', style: AcText.heading(size: 17, color: ac.ice)),
            const SizedBox(width: 10),
            Text('· ${_titles[_index]}', style: AcText.label(size: 11, color: ac.whiteDim)),
          ],
        ),
        actions: [
          const NotificationBell(),
          IconButton(
            onPressed: _openAccount,
            icon: const Icon(Icons.settings_outlined),
            tooltip: 'Tài khoản & cài đặt',
          ),
          const SizedBox(width: 4),
        ],
      ),
      body: SafeArea(top: false, child: IndexedStack(index: _index, children: screens)),
      bottomNavigationBar: DecoratedBox(
        decoration: BoxDecoration(border: Border(top: BorderSide(color: ac.carbonLine))),
        child: NavigationBar(
          selectedIndex: _index,
          onDestinationSelected: (i) => setState(() => _index = i),
          destinations: const [
            NavigationDestination(icon: Icon(Icons.insights_outlined), selectedIcon: Icon(Icons.insights), label: 'Trạng thái'),
            NavigationDestination(icon: Icon(Icons.settings_remote_outlined), selectedIcon: Icon(Icons.settings_remote), label: 'Điều khiển'),
            NavigationDestination(icon: Icon(Icons.school_outlined), selectedIcon: Icon(Icons.school), label: 'Học lệnh'),
          ],
        ),
      ),
    );
  }
}
