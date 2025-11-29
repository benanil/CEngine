#ifndef AX_STRING
#define AX_STRING

static inline int UTF8CharLen(const char* s)
{
    unsigned char c = (unsigned char)s[0];
    if ((c & 0x80) == 0) return 1;              // 0xxxxxxx (1-byte sequence)
    else if ((c & 0xE0) == 0xC0) return 2;      // 110xxxxx (2-byte sequence)
    else if ((c & 0xF0) == 0xE0) return 3;      // 1110xxxx (3-byte sequence)
    else if ((c & 0xF8) == 0xF0) return 4;      // 11110xxx (4-byte sequence)
    else return 0;                              // Invalid UTF-8 byte
}

// Returns the number of characters in an UTF-8 encoded string.
// (Does not check for encoding validity)
static inline int UTF8StrLen(const char *s)
{
    int len = 0;
    while (*s) {
        if ((*s & 0xC0) != 0x80) len++;
        s++;
    }
    return len;
}

// based on work of Christopher Wellons https://github.com/skeeto/branchless-utf8
// https://github.com/ocornut/imgui/blob/master/imgui.cpp
// Convert UTF-8 to 32-bit character, process single character input.
// A nearly-branchless UTF-8 decoder
// We handle UTF-8 decoding error by skipping forward. Returns len of utf8
static inline int CodepointFromUtf8(unsigned int* out_unicode, const char* in_text, const char* in_text_end)
{
    static const char lengths[32] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0 };
    static const int masks[]  = { 0x00, 0x7f, 0x1f, 0x0f, 0x07 };
    static const uint32_t mins[] = { 0x400000, 0, 0x80, 0x800, 0x10000 };
    static const int shiftc[] = { 0, 18, 12, 6, 0 };
    static const int shifte[] = { 0, 6, 4, 2, 0 };
    int len = lengths[*(const unsigned char*)in_text >> 3];
    int wanted = len + (len ? 0 : 1);

    if (in_text_end == NULL)
        in_text_end = in_text + wanted; // Max length, nulls will be taken into account.

    // Copy at most 'len' bytes, stop copying at 0 or past in_text_end. Branch predictor does a good job here,
    // so it is fast even with excessive branching.
    unsigned char s[4];
    s[0] = in_text + 0 < in_text_end ? in_text[0] : 0;
    s[1] = in_text + 1 < in_text_end ? in_text[1] : 0;
    s[2] = in_text + 2 < in_text_end ? in_text[2] : 0;
    s[3] = in_text + 3 < in_text_end ? in_text[3] : 0;

    // Assume a four-byte character and load four bytes. Unused bits are shifted out.
    *out_unicode  = (uint32_t)(s[0] & masks[len]) << 18;
    *out_unicode |= (uint32_t)(s[1] & 0x3f) << 12;
    *out_unicode |= (uint32_t)(s[2] & 0x3f) <<  6;
    *out_unicode |= (uint32_t)(s[3] & 0x3f) <<  0;
    *out_unicode >>= shiftc[len];
    
    const int UNICODE_CODEPOINT_MAX = 0xFFFF;
    // Accumulate the various error conditions.
    int e = 0;
    e  = (*out_unicode < mins[len]) << 6; // non-canonical encoding
    e |= ((*out_unicode >> 11) == 0x1b) << 7;  // surrogate half?
    e |= (*out_unicode > UNICODE_CODEPOINT_MAX) << 8;  // out of range?
    e |= (s[1] & 0xc0) >> 2;
    e |= (s[2] & 0xc0) >> 4;
    e |= (s[3]       ) >> 6;
    e ^= 0x2a; // top two bits of each tail byte correct?
    e >>= shifte[len];

    if (e) {
        // No bytes are consumed when *in_text == 0 || in_text == in_text_end.
        // One byte is consumed in case of invalid first byte of in_text.
        // All available bytes (at most `len` bytes) are consumed on incomplete/invalid second to last bytes.
        // Invalid or incomplete input may consume less bytes than wanted, therefore every byte has to be inspected in s.
        int get = !!s[0] + !!s[1] + !!s[2] + !!s[3];
        wanted = Min32(wanted, get);
        *out_unicode = (unsigned int)'!';
    }

    return wanted;
}

static inline uint32_t CodepointToUtf8(char* utf8, uint32_t unicode)
{
    if (unicode < 0x80u) {
        utf8[0] = unicode;
        return 1u;
    }
    if (unicode < 0x800u) {
        utf8[0] = (unicode >> 6)   | 0xC0u;
        utf8[1] = (unicode & 0x3Fu) | 0x80u;
        return 2u;
    }
    if (unicode < 0xFFFFu) {
        utf8[0] = ((unicode >> 12)       ) | 0xE0u;
        utf8[1] = ((unicode >> 6 ) & 0x3Fu) | 0x80u;
        utf8[2] = ((unicode      ) & 0x3Fu) | 0x80u;
        return 3u;
    }
    if (unicode <= 0x1fffffu) {
        /* http://tidy.sourceforge.net/cgi-bin/lxr/source/src/utf8.c#L380 */
        utf8[0] = (char)0xF0u | (unicode >> 18);
        utf8[1] = (char)0x80u | ((unicode >> 12) & 0x3Fu);
        utf8[2] = (char)0x80u | ((unicode >> 6) & 0x3Fu);
        utf8[3] = (char)0x80u | ((unicode & 0x3Fu));
        return 4u;
    }
    return 4u;
}

#endif