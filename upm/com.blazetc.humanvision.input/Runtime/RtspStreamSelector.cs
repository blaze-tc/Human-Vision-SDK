using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using UnityEngine;

namespace HumanVision.Input
{
    // Probe the requested RTSP URL and recognized same-channel peers before opening the decoder.
    internal static class RtspStreamSelector
    {
        private static readonly Regex Media = new Regex(@"^/media/video([123])$", RegexOptions.CultureInvariant);
        private static readonly Regex Unicast = new Regex(@"^(/unicast/c[1-9][0-9]*/s)([012])/live$", RegexOptions.CultureInvariant);
        private const int ProbeTimeoutMs = 2500;

        internal readonly struct Selection
        {
            public readonly Uri Address;
            public readonly Vector2Int Size;
            public readonly string Reason;

            public Selection(Uri address, Vector2Int size, string reason)
            {
                Address = address;
                Size = size;
                Reason = reason;
            }
        }

        public static Selection Select(Uri address, Vector2Int bounds, Func<Uri, Vector2Int> probe)
        {
            var available = Candidates(address).Select(candidate => new Selection(candidate, probe(candidate), null))
                .Where(candidate => candidate.Size.x > 0 && candidate.Size.y > 0).ToArray();
            var best = available.Where(candidate => candidate.Size.x <= bounds.x && candidate.Size.y <= bounds.y)
                .OrderByDescending(candidate => Pixels(candidate.Size)).FirstOrDefault();
            if (best.Address != null) return new Selection(best.Address, best.Size, "highest_within_limit");

            // If every confirmed stream exceeds the cap, favor the smallest decoder load.
            best = available.OrderBy(candidate => Pixels(candidate.Size)).FirstOrDefault();
            return best.Address != null
                ? new Selection(best.Address, best.Size, "above_limit_fallback")
                : new Selection(address, default, "unverified_requested_stream");
        }

        private static long Pixels(Vector2Int size) => (long)size.x * size.y;

        private static IEnumerable<Uri> Candidates(Uri address)
        {
            yield return address;
            string path = address.AbsolutePath;
            Match unicast = Unicast.Match(path);
            bool media = Media.IsMatch(path);
            if (!media && !unicast.Success) yield break;

            for (int number = 0; number < 3; number++) {
                string sibling = media ? $"/media/video{number + 1}" : $"{unicast.Groups[1].Value}{number}/live";
                if (sibling != path) yield return new UriBuilder(address) { Path = sibling }.Uri;
            }
        }

        public static string StreamLabel(Uri address)
        {
            if (address == null) return string.Empty;
            string path = address.AbsolutePath;
            Match media = Media.Match(path);
            Match unicast = Unicast.Match(path);
            if (!media.Success && !unicast.Success) return "指定 RTSP";
            char index = media.Success ? (char)(media.Groups[1].Value[0] - 1) : unicast.Groups[2].Value[0];
            string role = index == '0' ? "主码流" : index == '1' ? "辅码流" : "第三流";
            return $"{role} ({path})";
        }

        public static Vector2Int Probe(Uri address)
        {
            try {
                int status = NativeInputBindings.HV_Input_ProbeRtsp(address.AbsoluteUri, ProbeTimeoutMs, out int width, out int height);
                return status == 0 ? new Vector2Int(width, height) : default;
            } catch (DllNotFoundException) {
                return default;
            } catch (EntryPointNotFoundException) {
                return default;
            }
        }

    }
}
