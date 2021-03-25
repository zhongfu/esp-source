#include <string.h>
#include <stdlib.h>
#include <ctype.h>

void url_decode(char *dst, char *src) {
    char hex[2] = {0};

    size_t len = strlen(src);

    // indexes of next character to be compared
    size_t dst_d = 0;
    size_t src_d = 0;

    // starting indexes of uncopied portions
    size_t dst_s = 0;
    size_t src_s = 0;

    while (src_d < len) {
        if (src[src_d] == '%') { // uh oh!
            if (isxdigit(src[src_d + 1]) && isxdigit(src[src_d + 2])) { // is this really an escape code?
                hex[0] = src[src_d + 1];
                hex[1] = src[src_d + 2];
                char x = strtol(hex, NULL, 16); // put those chars in hex and convert to char

                // copy everything that we've yet to copy
                if (src_s != src_d) memcpy(dst + dst_s, src + src_s, src_d - src_s);

                dst[dst_d++] = x;
                src_d += 3;
                dst_s = dst_d;
                src_s = src_d;
                continue;
            }
        }

        dst_d++;
        src_d++;
    }
    
    // copy everything else that we need to copy
    if (src_s != src_d) memcpy(dst + dst_s, src + src_s, src_d - src_s);

    // add null term, just in case
    dst[dst_d] = 0;
}
