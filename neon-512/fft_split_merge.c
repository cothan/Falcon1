#include "inner.h"

/* 
 * Minimum logn: 5
 */
static void PQCLEAN_FALCON512_NEON_mergeFFT_log5(fpr *f, const fpr *f0, const fpr *f1, unsigned logn)
{
    // Total: 32 register
    float64x2x4_t f0_re, f0_im, f1_re, f1_im; // 16
    float64x2x4_t tmp;                        // 4
    float64x2x2_t s_tmp[4];                   // 8
    float64x2x2_t x_tmp, y_tmp;               // 4

    const unsigned int n = 1 << logn;
    const unsigned int hn = n >> 1;
    const unsigned int qn = n >> 2;
    int u1, u2;
    for (int u = 0; u < qn; u += 8)
    {
        u1 = u << 1;
        u2 = u1 + n;
        vloadx4(f0_re, &f0[u]);
        vloadx4(f1_re, &f1[u]);
        vloadx4(f0_im, &f0[u + qn]);
        vloadx4(f1_im, &f1[u + qn]);

        vload2(s_tmp[0], &fpr_gm_tab[u2 + 0]);
        vload2(s_tmp[1], &fpr_gm_tab[u2 + 4]);
        vload2(s_tmp[2], &fpr_gm_tab[u2 + 8]);
        vload2(s_tmp[3], &fpr_gm_tab[u2 + 12]);

        // f0,f1_re: 0-> 7
        // f0,f1_im: qn -> qn + 7
        vfmul(tmp.val[0], f1_re.val[0], s_tmp[0].val[0]);
        vfmul(tmp.val[1], f1_re.val[1], s_tmp[1].val[0]);
        vfmul(tmp.val[2], f1_re.val[2], s_tmp[2].val[0]);
        vfmul(tmp.val[3], f1_re.val[3], s_tmp[3].val[0]);

        vfms(tmp.val[0], tmp.val[0], f1_im.val[0], s_tmp[0].val[1]);
        vfms(tmp.val[1], tmp.val[1], f1_im.val[1], s_tmp[1].val[1]);
        vfms(tmp.val[2], tmp.val[2], f1_im.val[2], s_tmp[2].val[1]);
        vfms(tmp.val[3], tmp.val[3], f1_im.val[3], s_tmp[3].val[1]);

        // f_re0
        // vfaddx4(v0, f0_re, tmp);
        vfadd(x_tmp.val[0], f0_re.val[0], tmp.val[0]);
        vfsub(x_tmp.val[1], f0_re.val[0], tmp.val[0]);
        vfadd(y_tmp.val[0], f0_re.val[1], tmp.val[1]);
        vfsub(y_tmp.val[1], f0_re.val[1], tmp.val[1]);

        vstore2(&f[u1], x_tmp);
        vstore2(&f[u1 + 4], y_tmp);

        // f_re1
        // vfsubx4(v1, f0_re, tmp);
        vfadd(x_tmp.val[0], f0_re.val[2], tmp.val[2]);
        vfsub(x_tmp.val[1], f0_re.val[2], tmp.val[2]);
        vfadd(y_tmp.val[0], f0_re.val[3], tmp.val[3]);
        vfsub(y_tmp.val[1], f0_re.val[3], tmp.val[3]);

        vstore2(&f[u1 + 8], x_tmp);
        vstore2(&f[u1 + 12], y_tmp);

        vfmul(tmp.val[0], f1_re.val[0], s_tmp[0].val[1]);
        vfmul(tmp.val[1], f1_re.val[1], s_tmp[1].val[1]);
        vfmul(tmp.val[2], f1_re.val[2], s_tmp[2].val[1]);
        vfmul(tmp.val[3], f1_re.val[3], s_tmp[3].val[1]);

        vfma(tmp.val[0], tmp.val[0], f1_im.val[0], s_tmp[0].val[0]);
        vfma(tmp.val[1], tmp.val[1], f1_im.val[1], s_tmp[1].val[0]);
        vfma(tmp.val[2], tmp.val[2], f1_im.val[2], s_tmp[2].val[0]);
        vfma(tmp.val[3], tmp.val[3], f1_im.val[3], s_tmp[3].val[0]);

        // f_re0
        // vfaddx4(v0, f0_im, tmp);
        vfadd(x_tmp.val[0], f0_im.val[0], tmp.val[0]);
        vfsub(x_tmp.val[1], f0_im.val[0], tmp.val[0]);
        vfadd(y_tmp.val[0], f0_im.val[1], tmp.val[1]);
        vfsub(y_tmp.val[1], f0_im.val[1], tmp.val[1]);

        vstore2(&f[u1 + hn], x_tmp);
        vstore2(&f[u1 + hn + 4], y_tmp);

        // f_re1
        // vfsubx4(v1, f0_im, tmp);
        vfadd(x_tmp.val[0], f0_im.val[2], tmp.val[2]);
        vfsub(x_tmp.val[1], f0_im.val[2], tmp.val[2]);
        vfadd(y_tmp.val[0], f0_im.val[3], tmp.val[3]);
        vfsub(y_tmp.val[1], f0_im.val[3], tmp.val[3]);

        vstore2(&f[u1 + hn + 8], x_tmp);
        vstore2(&f[u1 + hn + 12], y_tmp);
    }
}

/* 
 * Fix logn: 4
 */
static inline void PQCLEAN_FALCON512_NEON_mergeFFT_log4(fpr *f, const fpr *f0, const fpr *f1)
{
    // Total: 20 register
    float64x2x4_t v0, v1, tmp;  // 12
    float64x2x2_t s_tmp[2];     // 4
    float64x2x2_t x_tmp, y_tmp; // 4

    vloadx4(v0, &f0[0]);
    vloadx4(v1, &f1[0]);
    vload2(s_tmp[0], &fpr_gm_tab[16]);
    vload2(s_tmp[1], &fpr_gm_tab[20]);

    vfmul(tmp.val[0], v1.val[0], s_tmp[0].val[0]);
    vfmul(tmp.val[1], v1.val[1], s_tmp[1].val[0]);
    vfmul(tmp.val[2], v1.val[0], s_tmp[0].val[1]);
    vfmul(tmp.val[3], v1.val[1], s_tmp[1].val[1]);

    vfms(tmp.val[0], tmp.val[0], v1.val[2], s_tmp[0].val[1]);
    vfms(tmp.val[1], tmp.val[1], v1.val[3], s_tmp[1].val[1]);
    vfma(tmp.val[2], tmp.val[2], v1.val[2], s_tmp[0].val[0]);
    vfma(tmp.val[3], tmp.val[3], v1.val[3], s_tmp[1].val[0]);

    vfadd(x_tmp.val[0], v0.val[0], tmp.val[0]);
    vfsub(x_tmp.val[1], v0.val[0], tmp.val[0]);
    vstore2(&f[0], x_tmp);

    vfadd(y_tmp.val[0], v0.val[1], tmp.val[1]);
    vfsub(y_tmp.val[1], v0.val[1], tmp.val[1]);
    vstore2(&f[4], y_tmp);

    vfadd(x_tmp.val[0], v0.val[2], tmp.val[2]);
    vfsub(x_tmp.val[1], v0.val[2], tmp.val[2]);
    vstore2(&f[8], x_tmp);

    vfadd(y_tmp.val[0], v0.val[3], tmp.val[3]);
    vfsub(y_tmp.val[1], v0.val[3], tmp.val[3]);
    vstore2(&f[12], y_tmp);
}

/* 
 * Fix logn: 3
 */
static inline void PQCLEAN_FALCON512_NEON_mergeFFT_log3(fpr *f, const fpr *f0, const fpr *f1)
{
    // Total: 12 register
    float64x2x2_t v0, v1, tmp;  // 6
    float64x2x2_t s_re_im;        // 2
    float64x2x2_t x_tmp, y_tmp; // 4

    vloadx2(v0, &f0[0]);
    vloadx2(v1, &f1[0]);
    vload2(s_re_im, &fpr_gm_tab[8]);

    vfmul(tmp.val[0], v1.val[0], s_re_im.val[0]);
    vfmul(tmp.val[1], v1.val[0], s_re_im.val[1]);

    vfms(tmp.val[0], tmp.val[0], v1.val[1], s_re_im.val[1]);
    vfma(tmp.val[1], tmp.val[1], v1.val[1], s_re_im.val[0]);

    vfadd(x_tmp.val[0], v0.val[0], tmp.val[0]);
    vfsub(x_tmp.val[1], v0.val[0], tmp.val[0]);

    vfadd(y_tmp.val[0], v0.val[1], tmp.val[1]);
    vfsub(y_tmp.val[1], v0.val[1], tmp.val[1]);

    vstore2(&f[0], x_tmp);
    vstore2(&f[4], y_tmp);
}

/* 
 * Only support logn >= 3
 */
void PQCLEAN_FALCON512_NEON_poly_merge_fft(fpr *f, const fpr *f0, const fpr *f1, unsigned logn)
{
    switch (logn)
    {
    case 3:
        PQCLEAN_FALCON512_NEON_mergeFFT_log3(f, f0, f1);
        break;
    case 4:
        PQCLEAN_FALCON512_NEON_mergeFFT_log4(f, f0, f1);
        break;
    default:
        PQCLEAN_FALCON512_NEON_mergeFFT_log5(f, f0, f1, logn);
    }
}

static inline void PQCLEAN_FALCON512_NEON_poly_splitFFT_log3(fpr *restrict f0, fpr *restrict f1, const fpr *f)
{
    // Total register: 14
    float64x2x2_t f_re, f_im;          // 4
    float64x2x2_t x, y, v, t, s_re_im; // 10

    vload2(f_re, &f[0]);
    vload2(f_im, &f[4]);
    vload2(s_re_im, &fpr_gm_tab_half[8]);

    vfadd(x.val[0], f_re.val[0], f_re.val[1]);
    vfadd(x.val[1], f_im.val[0], f_im.val[1]);

    vfmuln(x.val[0], x.val[0], 0.5);
    vfmuln(x.val[1], x.val[1], 0.5);

    vstorex2(&f0[0], x);

    // TODO: embed multiply with 0.5 to fpr_gm_tab_half table
    // vfmuln(s_re_im.val[0], s_re_im.val[0], 0.5);
    // vfmuln(s_re_im.val[1], s_re_im.val[1], 0.5);

    vfsub(v.val[0], f_re.val[0], f_re.val[1]);
    vfsub(v.val[1], f_im.val[0], f_im.val[1]);

    vfmul(t.val[0], v.val[0], s_re_im.val[0]);
    vfmul(t.val[1], v.val[1], s_re_im.val[0]);

    vfma(y.val[0], t.val[0], v.val[1], s_re_im.val[1]);
    vfms(y.val[1], t.val[1], v.val[0], s_re_im.val[1]);

    vstorex2(&f1[0], y);
}

static inline void PQCLEAN_FALCON512_NEON_poly_splitFFT_log4(fpr *restrict f0, fpr *restrict f1, const fpr *f)
{
    // Total register: 20
    float64x2x2_t x_tmp[2], y_tmp[2], s_tmp[2];    // 4
    float64x2x4_t x, y, t, v, f_re, f_im, s_re_im; //16

    vload2(x_tmp[0], &f[0]);
    vload2(x_tmp[1], &f[4]);
    vload2(y_tmp[0], &f[8]);
    vload2(y_tmp[1], &f[12]);
    vload2(s_tmp[0], &fpr_gm_tab_half[16]);
    vload2(s_tmp[1], &fpr_gm_tab_half[20]);

    s_re_im.val[0] = s_tmp[0].val[0];
    s_re_im.val[1] = s_tmp[0].val[1];
    s_re_im.val[2] = s_tmp[1].val[0];
    s_re_im.val[3] = s_tmp[1].val[1];

    // TODO: embed multiply with 0.5 to fpr_gm_tab_half table
    // vfmuln(s_re_im.val[0], s_re_im.val[0], 0.5);
    // vfmuln(s_re_im.val[1], s_re_im.val[1], 0.5);
    // vfmuln(s_re_im.val[2], s_re_im.val[2], 0.5);
    // vfmuln(s_re_im.val[3], s_re_im.val[3], 0.5);

    f_re.val[0] = x_tmp[0].val[0];
    f_re.val[1] = x_tmp[0].val[1];
    f_re.val[2] = x_tmp[1].val[0];
    f_re.val[3] = x_tmp[1].val[1];

    f_im.val[0] = y_tmp[0].val[0];
    f_im.val[1] = y_tmp[0].val[1];
    f_im.val[2] = y_tmp[1].val[0];
    f_im.val[3] = y_tmp[1].val[1];

    vfaddx4_swap(x, f_re, f_im, 0, 1, 2, 3);
    vfmuln(x.val[0], x.val[0], 0.5);
    vfmuln(x.val[1], x.val[1], 0.5);
    vfmuln(x.val[2], x.val[2], 0.5);
    vfmuln(x.val[3], x.val[3], 0.5);

    vfsubx4_swap(t, f_re, f_im, 0, 1, 2, 3);

    vfmul(v.val[0], t.val[2], s_re_im.val[1]);
    vfmul(v.val[1], t.val[3], s_re_im.val[3]);
    vfma(y.val[0], v.val[0], t.val[0], s_re_im.val[0]);
    vfma(y.val[1], v.val[1], t.val[1], s_re_im.val[2]);

    vfmul(v.val[2], t.val[2], s_re_im.val[0]);
    vfmul(v.val[3], t.val[3], s_re_im.val[2]);
    vfms(y.val[2], v.val[2], t.val[0], s_re_im.val[1]);
    vfms(y.val[3], v.val[3], t.val[1], s_re_im.val[3]);

    vstorex4(&f0[0], x);
    vstorex4(&f1[0], y);
}

void PQCLEAN_FALCON512_NEON_poly_splitFFT_log5(fpr *restrict f0, fpr *restrict f1, const fpr *f, unsigned logn)
{
    // Total register: 20
    float64x2x2_t x_tmp[2], y_tmp[2], s_tmp[2];    // 4
    float64x2x4_t x, y, t, v, f_re, f_im, s_re_im; //16


}

/* 
 * Only support logn >= 3
 */
void PQCLEAN_FALCON512_NEON_poly_split_fft(fpr *restrict f0, fpr *restrict f1, const fpr *f, unsigned logn)
{
    switch (logn)
    {
    case 3:
        PQCLEAN_FALCON512_NEON_poly_splitFFT_log3(f0, f1, f);
        break;

    case 4:
        PQCLEAN_FALCON512_NEON_poly_splitFFT_log4(f0, f1, f);
        break;

    default:
        PQCLEAN_FALCON512_NEON_poly_splitFFT_log5(f0, f1, f, logn);
    }
}
