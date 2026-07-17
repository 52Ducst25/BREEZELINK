import 'package:flutter/material.dart';

import '../screens/account/account_screen.dart';
import '../screens/control/control_screen.dart';
import '../screens/dashboard/dashboard_screen.dart';
import '../screens/devices/devices_screen.dart';
import '../theme/ac_colors.dart';

/// Main navigation shell: M3 [NavigationBar] over an [IndexedStack] so each
/// tab keeps its state (scroll position, in-flight form) when switching —
/// ported pattern from SafeKitchen's MainShell.
class MainShell extends StatefulWidget {
  const MainShell({super.key, required this.onLogout});

  final VoidCallback onLogout;

  @override
  State<MainShell> createState() => _MainShellState();
}

class _MainShellState extends State<MainShell> {
  int _index = 0;

  @override
  Widget build(BuildContext context) {
    final screens = [
      const DashboardScreen(),
      const DevicesScreen(),
      const ControlScreen(),
      AccountScreen(onLogout: widget.onLogout),
    ];

    return Scaffold(
      body: SafeArea(child: IndexedStack(index: _index, children: screens)),
      bottomNavigationBar: DecoratedBox(
        decoration: BoxDecoration(border: Border(top: BorderSide(color: context.ac.carbonLine))),
        child: NavigationBar(
          selectedIndex: _index,
          onDestinationSelected: (i) => setState(() => _index = i),
          destinations: const [
            NavigationDestination(icon: Icon(Icons.dashboard_outlined), selectedIcon: Icon(Icons.dashboard), label: 'TRẠNG THÁI'),
            NavigationDestination(icon: Icon(Icons.developer_board_outlined), selectedIcon: Icon(Icons.developer_board), label: 'THIẾT BỊ'),
            NavigationDestination(icon: Icon(Icons.tune_outlined), selectedIcon: Icon(Icons.tune), label: 'ĐIỀU KHIỂN'),
            NavigationDestination(icon: Icon(Icons.account_circle_outlined), selectedIcon: Icon(Icons.account_circle), label: 'TÀI KHOẢN'),
          ],
        ),
      ),
    );
  }
}
