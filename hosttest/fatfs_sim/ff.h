/*
 * A simulated FatFs for hosttest/dip_test.c: the calls dipfile.c makes, on one
 * file held in memory, with errors to order. Stands in for the real ff.h on the
 * include path, so the real dipfile.c is what gets tested.
 */
#ifndef FF_SIM_H
#define FF_SIM_H

#include <stdint.h>

typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef uint32_t FSIZE_t;

typedef enum
{
    FR_OK = 0,
    FR_DISK_ERR = 1,
    FR_NO_FILE = 4,
    FR_DENIED = 7
} FRESULT;

#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_CREATE_ALWAYS 0x08

typedef struct
{
    struct
    {
        FSIZE_t objsize;
    } obj;
    int open;
    FSIZE_t pos;
    BYTE mode;
} FIL;

#define f_size(fp) ((fp)->obj.objsize)

FRESULT f_open(FIL *fp, const char *path, BYTE mode);
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br);
FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw);
FRESULT f_close(FIL *fp);

#endif
