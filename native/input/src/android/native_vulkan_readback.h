#pragma once
namespace hvinput {
enum class NativeReadbackProof { WaitFailed, InvalidateFailed, Visible };
// A coherent allocation still needs the submission fence. Noncoherent staging
// additionally requires cache invalidation after that fence signals.
template <class Wait, class Invalidate>
NativeReadbackProof CompleteNativeReadback(bool coherent, Wait wait,
                                           Invalidate invalidate) {
  if (!wait())
    return NativeReadbackProof::WaitFailed;
  if (!coherent && !invalidate())
    return NativeReadbackProof::InvalidateFailed;
  return NativeReadbackProof::Visible;
}
} // namespace hvinput
