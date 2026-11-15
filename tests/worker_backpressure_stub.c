#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

enum { OUTPUT_LINES = 60000, PAYLOAD_BYTES = 1024 };

int main(int argc, char **argv)
{
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 4;
#endif
    char fill = 'X';
    if (argc >= 4) {
        FILE *source = fopen(argv[3], "rb");
        if (source) {
            char contents[4096];
            const size_t count = fread(contents, 1, sizeof(contents) - 1, source);
            contents[count] = '\0';
            if (strstr(contents, "YYYY") != NULL) fill = 'Y';
            fclose(source);
        }
    }

    char line[PAYLOAD_BYTES + 1];
    memset(line, fill, PAYLOAD_BYTES);
    line[PAYLOAD_BYTES] = '\n';
    for (size_t i = 0; i < OUTPUT_LINES; ++i) {
        if (fwrite(line, 1, sizeof(line), stdout) != sizeof(line)) return 2;
    }
    return fflush(stdout) == 0 ? 0 : 3;
}
