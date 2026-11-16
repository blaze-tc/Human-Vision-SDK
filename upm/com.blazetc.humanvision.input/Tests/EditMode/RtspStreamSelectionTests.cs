using System;
using System.Collections.Generic;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Input.Tests
{
    public sealed class RtspStreamSelectionTests
    {
        private static readonly Uri Main = new Uri("rtsp://camera:8554/media/video1");

        [Test]
        public void PicksHighestAvailableStreamWithinBounds()
        {
            var probes = new Dictionary<string, Vector2Int> {
                { "/media/video1", new Vector2Int(3840, 2160) },
                { "/media/video2", new Vector2Int(1280, 720) },
                { "/media/video3", new Vector2Int(1920, 1080) }
            };
            Assert.That(Choose(Main, probes), Is.EqualTo("/media/video3"));
            probes.Remove("/media/video3");
            Assert.That(Choose(Main, probes), Is.EqualTo("/media/video2"));
        }

        [Test]
        public void FallsBackToAvailable4KAndKeepsOriginalWhenUnconfirmed()
        {
            var probes = new Dictionary<string, Vector2Int> {
                { "/media/video1", new Vector2Int(3840, 2160) }
            };
            Assert.That(Choose(Main, probes), Is.EqualTo("/media/video1"));
            probes.Clear();
            Assert.That(Choose(Main, probes), Is.EqualTo("/media/video1"));
        }

        [Test]
        public void SameResolutionKeepsRequestedStreamAndGenericAddressStaysUsable()
        {
            var probes = new Dictionary<string, Vector2Int> {
                { "/media/video1", new Vector2Int(1280, 720) },
                { "/media/video2", new Vector2Int(1280, 720) }
            };
            Assert.That(Choose(Main, probes), Is.EqualTo("/media/video1"));
            Assert.That(Choose(new Uri("rtsp://camera/Exercise-8.mp4"), probes), Is.EqualTo("/Exercise-8.mp4"));
        }

        [Test]
        public void ProbesOnlyRequestedChannelAndRetainsCredentialsWithoutLoggingThem()
        {
            var requested = new Uri("rtsp://user:secret@camera:8554/unicast/c7/s0/live?transport=tcp");
            var selected = RtspStreamSelector.Select(requested, new Vector2Int(1920, 1080), candidate => {
                Assert.That(candidate.Host, Is.EqualTo(requested.Host));
                Assert.That(candidate.Port, Is.EqualTo(requested.Port));
                Assert.That(candidate.UserInfo, Is.EqualTo(requested.UserInfo));
                Assert.That(candidate.Query, Is.EqualTo(requested.Query));
                Assert.That(candidate.AbsolutePath, Does.StartWith("/unicast/c7/"));
                return candidate.AbsolutePath == "/unicast/c7/s1/live" ? new Vector2Int(720, 576) : default;
            });
            Assert.That(selected.Reason, Is.EqualTo("highest_within_limit"));
            Assert.That(RtspStreamSelector.StreamLabel(selected.Address), Is.EqualTo("辅码流 (/unicast/c7/s1/live)"));
        }

        private static string Choose(Uri address, Dictionary<string, Vector2Int> probes)
        {
            Func<Uri, Vector2Int> probe = uri => probes.TryGetValue(uri.AbsolutePath, out var size) ? size : default;
            return RtspStreamSelector.Select(address, new Vector2Int(1920, 1080), probe).Address.AbsolutePath;
        }
    }
}
