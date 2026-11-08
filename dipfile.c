/*
 * The DIP switches of a game as a text file, see dipfile.h.
 */
#include <stdio.h>
#include <string.h>

#include "dipfile.h"

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* "dswN=xx" into its port. Any other line changes nothing. */
static void parse_line(const char *line, size_t len, uint8_t *ports)
{
    if (len != 7 || memcmp(line, "dsw", 3) != 0 || line[4] != '=')
        return;
    const int port = line[3] - '0';
    const int hi = hex_digit(line[5]), lo = hex_digit(line[6]);
    if (port >= 0 && port < GAL_DIP_PORTS && hi >= 0 && lo >= 0)
        ports[port] = (uint8_t)(hi * 16 + lo);
}

int dipFileLoad(FIL *fil, const char *path, const gal_dip_t *rows, int nrows, uint8_t *ports)
{
    char text[DIPFILE_MAX];
    UINT n = 0;
    if (f_open(fil, path, FA_READ) != FR_OK)
        return 0;
    const int ok = f_size(fil) <= sizeof(text) && f_read(fil, text, sizeof(text), &n) == FR_OK;
    f_close(fil);
    if (!ok)
        return 0;

    uint8_t saved[GAL_DIP_PORTS];
    memcpy(saved, ports, sizeof(saved));
    for (UINT start = 0; start < n;)
    {
        UINT end = start;
        while (end < n && text[end] != '\r' && text[end] != '\n')
            end++;
        parse_line(text + start, end - start, saved);
        start = end + 1;
    }
    for (int k = 0; k < nrows; k++)
        gal_dip_select(&rows[k], ports, gal_dip_choice(&rows[k], saved));
    return 1;
}

int dipFileSave(FIL *fil, const char *path, const gal_dip_t *rows, int nrows, const uint8_t *ports)
{
    char text[DIPFILE_MAX + 1];
    UINT len = 0, written = 0;
    for (int p = 0; p < GAL_DIP_PORTS; p++)
        for (int k = 0; k < nrows; k++)
            if (rows[k].port == p)
            {
                len += (UINT)snprintf(text + len, sizeof(text) - len, "dsw%d=%02x\n", p, ports[p]);
                break;
            }
    if (f_open(fil, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return 0;
    const FRESULT wr = f_write(fil, text, len, &written);
    const FRESULT cl = f_close(fil);
    return wr == FR_OK && cl == FR_OK && written == len;
}
