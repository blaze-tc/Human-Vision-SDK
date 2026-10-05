using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace HumanVision
{
    // Configuration only. JsonUtility cannot reject duplicate keys or distinguish missing
    // false/zero values from declared ones; deployment contracts require both checks.
    internal sealed class HumanVisionConfigurationJson
    {
        private readonly string text;
        private int position;
        private HumanVisionConfigurationJson(string text) { this.text = text; }
        internal static object Parse(string text)
        {
            if (text == null || text.Length > 4 * 1024 * 1024) throw new InvalidDataException("Invalid configuration JSON size.");
            var parser = new HumanVisionConfigurationJson(text);
            object result = parser.Value(0); parser.Space();
            if (parser.position != text.Length) throw new InvalidDataException("Trailing configuration JSON content.");
            return result;
        }
        private void Space() { while (position < text.Length && (text[position] == ' ' || text[position] == '\r' || text[position] == '\n' || text[position] == '\t')) position++; }
        private bool Take(char value) { Space(); if (position < text.Length && text[position] == value) { position++; return true; } return false; }
        private void Require(char value) { if (!Take(value)) throw new InvalidDataException("Invalid configuration JSON at " + position + "."); }
        private object Value(int depth)
        {
            Space(); if (depth > 64 || position >= text.Length) throw new InvalidDataException("Invalid configuration JSON nesting/end.");
            if (text[position] == '{')
            {
                position++; var rows = new Dictionary<string, object>(StringComparer.Ordinal);
                if (Take('}')) return rows;
                do { Space(); string key = String(); Require(':'); if (rows.ContainsKey(key)) throw new InvalidDataException("Duplicate configuration JSON key: " + key); rows.Add(key, Value(depth + 1)); } while (Take(','));
                Require('}'); return rows;
            }
            if (text[position] == '[')
            {
                position++; var rows = new List<object>(); if (Take(']')) return rows;
                do { if (rows.Count >= 10000) throw new InvalidDataException("Configuration array too large."); rows.Add(Value(depth + 1)); } while (Take(','));
                Require(']'); return rows;
            }
            if (text[position] == '"') return String();
            foreach (string token in new[] { "true", "false", "null" })
                if (position + token.Length <= text.Length && string.CompareOrdinal(text, position, token, 0, token.Length) == 0) { position += token.Length; return token == "null" ? null : (object)(token == "true"); }
            int start = position; if (text[position] == '-') position++;
            if (position >= text.Length || text[position] < '0' || text[position] > '9') throw new InvalidDataException("Invalid JSON number.");
            if (text[position] == '0') position++; else while (position < text.Length && text[position] >= '0' && text[position] <= '9') position++;
            bool integer = true;
            if (position < text.Length && text[position] == '.') { integer = false; position++; Digits(); }
            if (position < text.Length && (text[position] == 'e' || text[position] == 'E')) { integer = false; position++; if (position < text.Length && (text[position] == '+' || text[position] == '-')) position++; Digits(); }
            string number = text.Substring(start, position - start);
            if (integer && long.TryParse(number, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out long whole)) return whole;
            if (!double.TryParse(number, NumberStyles.Float, CultureInfo.InvariantCulture, out double fraction) || double.IsInfinity(fraction) || double.IsNaN(fraction)) throw new InvalidDataException("Invalid JSON number range.");
            return fraction;
        }
        private void Digits() { int start = position; while (position < text.Length && text[position] >= '0' && text[position] <= '9') position++; if (start == position) throw new InvalidDataException("Missing JSON numeric digits."); }
        private string String()
        {
            Require('"'); var result = new StringBuilder();
            while (position < text.Length)
            {
                char value = text[position++]; if (value == '"') return result.ToString();
                if (value < 32) throw new InvalidDataException("Unescaped JSON control character.");
                if (value != '\\') { result.Append(value); continue; }
                if (position >= text.Length) throw new InvalidDataException("Incomplete JSON escape.");
                value = text[position++];
                switch (value)
                {
                    case '"': case '\\': case '/': result.Append(value); break;
                    case 'b': result.Append('\b'); break; case 'f': result.Append('\f'); break;
                    case 'n': result.Append('\n'); break; case 'r': result.Append('\r'); break; case 't': result.Append('\t'); break;
                    case 'u':
                        if (position + 4 > text.Length || !ushort.TryParse(text.Substring(position, 4), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out ushort code)) throw new InvalidDataException("Invalid JSON unicode escape.");
                        result.Append((char)code); position += 4; break;
                    default: throw new InvalidDataException("Invalid JSON string escape.");
                }
            }
            throw new InvalidDataException("Unterminated JSON string.");
        }
        internal static Dictionary<string, object> Object(object value) => value as Dictionary<string, object> ?? throw new InvalidDataException("Expected configuration object.");
        internal static List<object> Array(object value) => value as List<object> ?? throw new InvalidDataException("Expected configuration array.");
        internal static object Field(Dictionary<string, object> value, string key) => value.TryGetValue(key, out object result) ? result : throw new InvalidDataException("Missing configuration field: " + key);
        internal static string Text(Dictionary<string, object> value, string key) => Field(value, key) as string ?? throw new InvalidDataException("Expected string: " + key);
        internal static long Integer(Dictionary<string, object> value, string key) => Field(value, key) is long number ? number : throw new InvalidDataException("Expected integer: " + key);
        internal static bool Boolean(Dictionary<string, object> value, string key) => Field(value, key) is bool flag ? flag : throw new InvalidDataException("Expected boolean: " + key);
        internal static void Keys(Dictionary<string, object> value, params string[] expected)
        {
            if (value.Count != expected.Length) throw new InvalidDataException("Unexpected configuration fields.");
            foreach (string key in expected) Field(value, key);
        }
    }
}
