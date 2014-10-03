/* Copyright (C) 2014 by Maxim Bublis <b@codemonkey.ru>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "utils.h"

#include <string.h>

const char *
cstring_skip_prefix(const char *src, const char *prefix)
{
    size_t len = strlen(prefix);

    if (strncmp(src, prefix, len) == 0) {
        return src + len;
    }

    return NULL;
}

// Converts hex character into a nibble (4 bit value).
static uint8_t
nibble_from_char(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return 255;
}

// Converts nibble (4 bit value) into a hex character representation.
static char
char_from_nibble(uint8_t nibble)
{
    if (nibble >= 0 && nibble <= 9) {
        return nibble + '0';
    }
    else if (nibble >= 10 && nibble <= 15) {
        return nibble + 'a' - 10;
    }

    return '*';
}

git_svn_status_t
hex_to_bytes(uint8_t *bytes, const char *src, size_t bytesLen)
{
    size_t len = strlen(src);

    if (len < bytesLen * 2) {
        return GIT_SVN_FAILURE;
    }

    for (int i = 0; i < bytesLen; i++) {
        bytes[i] = (nibble_from_char(src[i * 2]) << 4 | nibble_from_char(src[i * 2 + 1]));
    }

    return GIT_SVN_SUCCESS;
}

git_svn_status_t
bytes_to_hex(char *dst, const uint8_t *bytes, size_t bytesLen)
{
    for (int i = 0; i < bytesLen; i++) {
        dst[i * 2] = char_from_nibble(bytes[i] >> 4);
        dst[i * 2 + 1] = char_from_nibble(bytes[i] & 0x0f);
    }

    return GIT_SVN_SUCCESS;
}
