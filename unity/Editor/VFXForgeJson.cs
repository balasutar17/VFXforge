// VFX Forge: a small JSON reader for .vfxforge files.
//
// Unity's JsonUtility cannot read free-form JSON, and not every project has
// a JSON package, so the importer brings its own. Objects become
// Dictionary<string, object>, arrays List<object>, numbers double.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace VFXForge.EditorTools
{
    public static class VFXForgeJson
    {
        public static object Parse(string text)
        {
            var reader = new Reader(text);
            object value = reader.ReadValue();
            reader.SkipSpace();
            if (!reader.AtEnd)
                throw new FormatException("Unexpected text after the end of the JSON data.");
            return value;
        }

        sealed class Reader
        {
            readonly string text;
            int at;

            public Reader(string text) { this.text = text ?? string.Empty; }

            public bool AtEnd { get { return at >= text.Length; } }

            public void SkipSpace()
            {
                while (at < text.Length && char.IsWhiteSpace(text[at]))
                    at++;
            }

            char Next()
            {
                if (at >= text.Length)
                    throw new FormatException("The JSON data ends too early.");
                return text[at++];
            }

            void Expect(string word)
            {
                for (int i = 0; i < word.Length; i++)
                    if (Next() != word[i])
                        throw new FormatException("Expected '" + word + "' in the JSON data.");
            }

            public object ReadValue()
            {
                SkipSpace();
                if (AtEnd)
                    throw new FormatException("The JSON data ends too early.");
                char c = text[at];
                switch (c)
                {
                    case '{': return ReadObject();
                    case '[': return ReadArray();
                    case '"': return ReadString();
                    case 't': Expect("true"); return true;
                    case 'f': Expect("false"); return false;
                    case 'n': Expect("null"); return null;
                    default: return ReadNumber();
                }
            }

            Dictionary<string, object> ReadObject()
            {
                var result = new Dictionary<string, object>();
                Next();
                SkipSpace();
                if (!AtEnd && text[at] == '}') { at++; return result; }
                while (true)
                {
                    SkipSpace();
                    string key = ReadString();
                    SkipSpace();
                    if (Next() != ':')
                        throw new FormatException("Expected ':' in the JSON data.");
                    result[key] = ReadValue();
                    SkipSpace();
                    char c = Next();
                    if (c == '}') return result;
                    if (c != ',')
                        throw new FormatException("Expected ',' or '}' in the JSON data.");
                }
            }

            List<object> ReadArray()
            {
                var result = new List<object>();
                Next();
                SkipSpace();
                if (!AtEnd && text[at] == ']') { at++; return result; }
                while (true)
                {
                    result.Add(ReadValue());
                    SkipSpace();
                    char c = Next();
                    if (c == ']') return result;
                    if (c != ',')
                        throw new FormatException("Expected ',' or ']' in the JSON data.");
                }
            }

            string ReadString()
            {
                if (Next() != '"')
                    throw new FormatException("Expected a string in the JSON data.");
                var sb = new StringBuilder();
                while (true)
                {
                    char c = Next();
                    if (c == '"') return sb.ToString();
                    if (c != '\\') { sb.Append(c); continue; }
                    char e = Next();
                    switch (e)
                    {
                        case '"': sb.Append('"'); break;
                        case '\\': sb.Append('\\'); break;
                        case '/': sb.Append('/'); break;
                        case 'b': sb.Append('\b'); break;
                        case 'f': sb.Append('\f'); break;
                        case 'n': sb.Append('\n'); break;
                        case 'r': sb.Append('\r'); break;
                        case 't': sb.Append('\t'); break;
                        case 'u':
                            if (at + 4 > text.Length)
                                throw new FormatException("A \\u escape ends too early.");
                            sb.Append((char)int.Parse(text.Substring(at, 4), NumberStyles.HexNumber, CultureInfo.InvariantCulture));
                            at += 4;
                            break;
                        default:
                            throw new FormatException("Unknown escape in a JSON string.");
                    }
                }
            }

            object ReadNumber()
            {
                int start = at;
                while (at < text.Length && "+-0123456789.eE".IndexOf(text[at]) >= 0)
                    at++;
                if (start == at)
                    throw new FormatException("Unexpected character '" + text[at] + "' in the JSON data.");
                return double.Parse(text.Substring(start, at - start), NumberStyles.Float, CultureInfo.InvariantCulture);
            }
        }
    }
}
