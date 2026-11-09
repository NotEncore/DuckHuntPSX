#include "guncal.h"
#include <stdio.h>

const int guncal_tx[GUNCAL_POINTS] = { 80, 240, 160, 80, 240 };
const int guncal_ty[GUNCAL_POINTS] = { 104, 104, 148, 188, 188 };

int guncon_has_light(uint16_t gun_x, uint16_t gun_y)
{
	if (gun_x == GUN_ERR_X)
		return 0;
	if (gun_y == GUN_ERR_Y_A || gun_y == GUN_ERR_Y_B)
		return 0;
	if (gun_x == 0 && gun_y == 0)
		return 0;
	return 1;
}

void map_gun_to_screen(uint16_t gun_x, uint16_t gun_y, int *sx, int *sy)
{
	int x_span = GUN_X_MAX - GUN_X_MIN;
	int y_span = GUN_Y_MAX_NTSC - GUN_Y_MIN_NTSC;
	int x = ((int)gun_x - GUN_X_MIN) * (GUNCAL_SCREEN_X - 1) / x_span;
	int y = ((int)gun_y - GUN_Y_MIN_NTSC) * (GUNCAL_SCREEN_Y - 1) / y_span;

	if (x < 0)
		x = 0;
	if (x > GUNCAL_SCREEN_X - 1)
		x = GUNCAL_SCREEN_X - 1;
	if (y < 0)
		y = 0;
	if (y > GUNCAL_SCREEN_Y - 1)
		y = GUNCAL_SCREEN_Y - 1;
	*sx = x;
	*sy = y;
}

void guncal_reset(GunCal *c)
{
	int i;

	c->has_cal = 0;
	c->use_scale = 0;
	c->sx = GUNCAL_FP_ONE;
	c->sy = GUNCAL_FP_ONE;
	c->ox = 0;
	c->oy = 0;
	c->dropped_all = 0;
	for (i = 0; i < GUNCAL_POINTS; i++) {
		int k;

		c->pt[i].n = 0;
		c->pt[i].mean_mx = c->pt[i].mean_my = 0;
		c->pt[i].mean_rx = c->pt[i].mean_ry = 0;
		c->pt[i].kept = c->pt[i].dropped = 0;
		for (k = 0; k < GUNCAL_SHOTS; k++)
			c->pt[i].mx[k] = c->pt[i].my[k] = c->pt[i].rx[k] = c->pt[i].ry[k] = 0;
	}
}

int guncal_add_shot(GunCal *c, int point, int orig_x, int orig_y,
	int raw_x, int raw_y)
{
	GunCalPoint *p;

	if (point < 0 || point >= GUNCAL_POINTS)
		return 0;
	p = &c->pt[point];
	if (p->n >= GUNCAL_SHOTS)
		return 0;
	p->mx[p->n] = orig_x;
	p->my[p->n] = orig_y;
	p->rx[p->n] = raw_x;
	p->ry[p->n] = raw_y;
	p->n++;
	return 1;
}

int guncal_point_full(const GunCal *c, int point)
{
	if (point < 0 || point >= GUNCAL_POINTS)
		return 1;
	return c->pt[point].n >= GUNCAL_SHOTS;
}

void guncal_apply(const GunCal *c, int orig_x, int orig_y, int *sx, int *sy)
{
	int x = orig_x;
	int y = orig_y;

	if (c->has_cal) {
		if (c->use_scale) {
			x = (int)(((long long)orig_x * c->sx + 32768) >> 16);
			y = (int)(((long long)orig_y * c->sy + 32768) >> 16);
		}
		x += c->ox;
		y += c->oy;
	}
	if (x < 0)
		x = 0;
	if (x > GUNCAL_SCREEN_X - 1)
		x = GUNCAL_SCREEN_X - 1;
	if (y < 0)
		y = 0;
	if (y > GUNCAL_SCREEN_Y - 1)
		y = GUNCAL_SCREEN_Y - 1;
	*sx = x;
	*sy = y;
}

static int iabs(int v)
{
	return (v < 0) ? -v : v;
}

static int round_div(int sum, int n)
{
	if (n <= 0)
		return 0;
	if (sum >= 0)
		return (sum + n / 2) / n;
	return -((-sum + n / 2) / n);
}

static void sort_ints(int *a, int n)
{
	int i, j, t;

	for (i = 1; i < n; i++) {
		t = a[i];
		j = i;
		while (j > 0 && a[j - 1] > t) {
			a[j] = a[j - 1];
			j--;
		}
		a[j] = t;
	}
}

static int median_of(const int *src, int n)
{
	int tmp[GUNCAL_SHOTS];
	int i;

	for (i = 0; i < n; i++)
		tmp[i] = src[i];
	sort_ints(tmp, n);
	return tmp[n / 2];
}

void guncal_fmt_scale(char *out, int fp)
{
	int neg = fp < 0;
	int a = iabs(fp);
	int ip = a / GUNCAL_FP_ONE;
	int frac = a % GUNCAL_FP_ONE;

	frac = (int)(((long long)frac * 100 + 32768) >> 16);
	if (frac >= 100) {
		frac = 0;
		ip++;
	}
	if (neg && (ip || frac))
		sprintf(out, "-%d.%02d", ip, frac);
	else
		sprintf(out, "%d.%02d", ip, frac);
}

static void linreg(const int *m, const int *t, int n, int *s_fp, int *off)
{
	long long sum_m = 0, sum_t = 0, sum_mt = 0, sum_mm = 0;
	long long num, den;
	int i;

	if (n <= 1) {
		*s_fp = GUNCAL_FP_ONE;
		*off = 0;
		return;
	}
	for (i = 0; i < n; i++) {
		sum_m += m[i];
		sum_t += t[i];
		sum_mt += (long long)m[i] * t[i];
		sum_mm += (long long)m[i] * m[i];
	}
	den = (long long)n * sum_mm - sum_m * sum_m;
	num = (long long)n * sum_mt - sum_m * sum_t;
	if (den == 0) {
		*s_fp = GUNCAL_FP_ONE;
		*off = (int)round_div((int)(sum_t - sum_m), n);
		return;
	}
	*s_fp = (int)((num << 16) / den);
	*off = (int)((sum_t - (((long long)*s_fp * sum_m) >> 16)) / n);
}

void guncal_compute(GunCal *c)
{
	guncal_compute_to(c, guncal_tx, guncal_ty);
}

void guncal_compute_to(GunCal *c, const int *txp, const int *typ)
{
	guncal_compute_to_min(c, txp, typ, 5);
}

void guncal_compute_to_min(GunCal *c, const int *txp, const int *typ, int min_keep)
{
	int i, k;
	int mx[GUNCAL_POINTS], my[GUNCAL_POINTS], tx[GUNCAL_POINTS], ty[GUNCAL_POINTS];
	int npts = 0;
	int sx, sy, ox, oy;
	int mean_off_x = 0, mean_off_y = 0;
	int use_s;

	if (min_keep < 1)
		min_keep = 1;
	c->dropped_all = 0;
	for (i = 0; i < GUNCAL_POINTS; i++) {
		GunCalPoint *p = &c->pt[i];
		int medx, medy, sxm = 0, sym = 0, srx = 0, sry = 0, kept = 0;

		if (p->n < min_keep)
			continue;
		medx = median_of(p->mx, p->n);
		medy = median_of(p->my, p->n);
		p->dropped = 0;
		for (k = 0; k < p->n; k++) {
			if (iabs(p->mx[k] - medx) > GUNCAL_OUTLIER_PX ||
				iabs(p->my[k] - medy) > GUNCAL_OUTLIER_PX) {
				p->dropped++;
				continue;
			}
			sxm += p->mx[k];
			sym += p->my[k];
			srx += p->rx[k];
			sry += p->ry[k];
			kept++;
		}
		if (kept < min_keep) {
			sxm = sym = srx = sry = 0;
			kept = p->n;
			p->dropped = 0;
			for (k = 0; k < p->n; k++) {
				sxm += p->mx[k];
				sym += p->my[k];
				srx += p->rx[k];
				sry += p->ry[k];
			}
		}
		p->kept = kept;
		p->mean_mx = round_div(sxm, kept);
		p->mean_my = round_div(sym, kept);
		p->mean_rx = round_div(srx, kept);
		p->mean_ry = round_div(sry, kept);
		c->dropped_all += p->dropped;
		mx[npts] = p->mean_mx;
		my[npts] = p->mean_my;
		tx[npts] = txp[i];
		ty[npts] = typ[i];
		mean_off_x += tx[npts] - mx[npts];
		mean_off_y += ty[npts] - my[npts];
		npts++;
	}

	c->sx = GUNCAL_FP_ONE;
	c->sy = GUNCAL_FP_ONE;
	c->ox = round_div(mean_off_x, npts);
	c->oy = round_div(mean_off_y, npts);
	c->use_scale = 0;
	c->has_cal = (npts >= 3);

	if (npts < 3)
		return;

	linreg(mx, tx, npts, &sx, &ox);
	linreg(my, ty, npts, &sy, &oy);
	use_s = (iabs(sx - GUNCAL_FP_ONE) > (GUNCAL_FP_ONE * GUNCAL_SCALE_PCT) / 100) ||
		(iabs(sy - GUNCAL_FP_ONE) > (GUNCAL_FP_ONE * GUNCAL_SCALE_PCT) / 100);
	if (use_s) {
		c->use_scale = 1;
		c->sx = sx;
		c->sy = sy;
		c->ox = ox;
		c->oy = oy;
	}
}
