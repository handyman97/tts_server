//

#include "sink.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int
sink::consume (const char* filename)
{
    int fd = open (filename, O_RDONLY);
    if (fd < 0) return (-1);

    int rslt = consume (fd);
    close (fd);

    return (rslt);
}

int
sink::consume (const uint8_t* wav, size_t len)
{
    FILE* f = fmemopen ((uint8_t*)wav, len, "r");
    if (!f) return -1;
    int fd = fileno (f);
    int rslt = consume (fd);
    fclose (f);

    return rslt;
}
