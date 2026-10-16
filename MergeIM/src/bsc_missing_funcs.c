/* AR: For MN5. These are functions that SCEC commented out in misc.c and wcc_add.c.
   These were present in earlier SCEC versions; removed without updating callers. */

static long frandx = 1;

double frand(void)
{
    frandx = (frandx * 1103515245 + 12345) & 0x7fffffff;
    return((double)(frandx)/1073741824.0 - 1.0);
}

double sfrand(long *seed)
{
    *seed = ((*seed) * 1103515245 + 12345) & 0x7fffffff;
    return((double)(*seed)/1073741824.0 - 1.0);
}

void zapit(float *s, int n)
{
    while(n--)
    {
        s[0] = 0.0;
        s++;
    }
}
