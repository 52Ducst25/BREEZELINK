/// Classifies a temperature reading into a thermal state relative to the
/// ASHRAE-adaptive comfort band, so color always encodes MEANING (how hot/cold
/// relative to comfort) rather than sensor identity (indoor vs outdoor use the
/// exact same scale — see brief's color-encodes-thermal-state requirement).
enum ThermalState { cold, neutral, warm, hot }

/// [neutral] is the comfort-pipeline's `t_neutral` (or `t_target`) for this
/// cycle when known; falls back to a generic 24 degC band when the pipeline
/// has not produced one yet (e.g. before first outdoor reading).
ThermalState classifyThermal(double temp, {double neutral = 24}) {
  final delta = temp - neutral;
  if (delta <= -2) return ThermalState.cold;
  if (delta >= 4) return ThermalState.hot;
  if (delta >= 2) return ThermalState.warm;
  return ThermalState.neutral;
}

/// Resolve this state against caller-supplied per-state values — widgets pass
/// their four `context.ac.thermalXxx` colors (or any other typed values).
extension ThermalStateResolve on ThermalState {
  T colorFor<T>({required T cold, required T neutral, required T warm, required T hot}) {
    switch (this) {
      case ThermalState.cold:
        return cold;
      case ThermalState.neutral:
        return neutral;
      case ThermalState.warm:
        return warm;
      case ThermalState.hot:
        return hot;
    }
  }
}
