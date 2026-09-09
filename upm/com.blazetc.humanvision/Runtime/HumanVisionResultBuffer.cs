using System;
using System.Runtime.InteropServices;
using HumanVision.Interop;
using UnityEngine;

namespace HumanVision
{
    internal sealed class HumanVisionResultBuffer : IDisposable
    {
        private IntPtr _nativeBodies;
        private IntPtr _nativeHands;
        internal IntPtr NativeHands => _nativeHands;
        private bool _disposed;

        internal HumanVisionResultBuffer(int capacity)
        {
            if (capacity < 1)
            {
                throw new ArgumentOutOfRangeException(nameof(capacity));
            }

            Bodies = Array.Empty<HumanVisionBody>();
            EnsureCapacity(capacity);
        }

        internal HumanVisionBody[] Bodies { get; private set; }
        internal int Capacity => Bodies.Length;
        internal IntPtr NativeBodies => _nativeBodies;
        internal int BodyCount { get; private set; }
        internal long ResultSequence { get; private set; }
        internal long SourceFrameId { get; private set; }
        internal long SourceTimestampUs { get; private set; }

        internal void EnsureCapacity(int capacity)
        {
            ThrowIfDisposed();
            if (capacity <= Capacity)
            {
                return;
            }

            var grown = new HumanVisionBody[capacity];
            Array.Copy(Bodies, grown, Bodies.Length);
            for (int index = Bodies.Length; index < grown.Length; index++)
            {
                grown[index] = new HumanVisionBody();
            }

            int bytes = checked(capacity * NativeBindings.BodySize);
            _nativeBodies = _nativeBodies == IntPtr.Zero
                ? Marshal.AllocHGlobal(bytes)
                : Marshal.ReAllocHGlobal(_nativeBodies, (IntPtr)bytes);
            int handBytes = checked(capacity * 6 * Marshal.SizeOf<HVJointNative>());
            _nativeHands = _nativeHands == IntPtr.Zero ? Marshal.AllocHGlobal(handBytes) : Marshal.ReAllocHGlobal(_nativeHands, (IntPtr)handBytes);
            Bodies = grown;
        }

        internal unsafe void CopyFromNative(
            int count,
            long resultSequence,
            long sourceFrameId,
            long sourceTimestampUs, bool hasHands = false)
        {
            ThrowIfDisposed();
            if (count < 0 || count > Capacity)
            {
                throw new ArgumentOutOfRangeException(nameof(count));
            }

            var nativeBodies = (HVBodyNative*)_nativeBodies.ToPointer();
            for (int bodyIndex = 0; bodyIndex < count; bodyIndex++)
            {
                HVBodyNative* nativeBody = nativeBodies + bodyIndex;
                HumanVisionBody body = Bodies[bodyIndex];
                body.TrackId = nativeBody->TrackId;
                body.BoundingBoxPixels = new Rect(
                    nativeBody->BoundingBox.X,
                    nativeBody->BoundingBox.Y,
                    nativeBody->BoundingBox.Width,
                    nativeBody->BoundingBox.Height);
                body.DetectionConfidence = nativeBody->DetectionConfidence;

                for (int h = 0; h < 6; h++) {
                    HVJointNative* hand = (HVJointNative*)_nativeHands.ToPointer() + bodyIndex * 6 + h;
                    body.HandJoints[h] = hasHands ? new HumanVisionJoint(new Vector2(hand->X, hand->Y),
                        new Vector2(hand->NormalizedX, hand->NormalizedY), hand->Confidence,
                        hand->Valid != 0, hand->Reserved0 != 0) : default;
                }
                HVJointNative* nativeJoints = &nativeBody->Joint0;
                for (int jointIndex = 0; jointIndex < HumanVisionJoint.Count; jointIndex++)
                {
                    HVJointNative* nativeJoint = nativeJoints + jointIndex;
                    body.Joints[jointIndex] = new HumanVisionJoint(
                        new Vector2(nativeJoint->X, nativeJoint->Y),
                        new Vector2(nativeJoint->NormalizedX, nativeJoint->NormalizedY),
                        nativeJoint->Confidence,
                        nativeJoint->Valid != 0);
                }
            }

            BodyCount = count;
            ResultSequence = resultSequence;
            SourceFrameId = sourceFrameId;
            SourceTimestampUs = sourceTimestampUs;
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            if (_nativeBodies != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(_nativeBodies);
                _nativeBodies = IntPtr.Zero;
            }

            if (_nativeHands != IntPtr.Zero) { Marshal.FreeHGlobal(_nativeHands); _nativeHands = IntPtr.Zero; }
            _disposed = true;
        }

        private void ThrowIfDisposed()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(HumanVisionResultBuffer));
            }
        }
    }
}
