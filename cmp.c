/*
 *  Copyright 2014-2015 Ian Liu Rodrigues <ian.liu88@gmail.com>
 *
 *  This file is part of CMP.
 *
 *  CMP is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  CMP is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with CMP.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include <math.h>
#include <vector.h>
#include <uthash.h>
#include <interpol.h>
#include <semblance.h>
#include <log.h>
#include <su.h>

struct cdp_traces {
	int cdp;
	vector_t(su_trace_t) traces;
	UT_hash_handle hh;
};

float getmax_C(aperture_t *ap, int t0s, float c0, float c1, int nc, float *sem, float *stack)
{
	float Copt;
	float smax = 0;
	float _stack = 0;
	for (int i = 0; i < nc; i++) {
		float C = c0 + (c1 - c0) * i / nc;
		float m0x, m0y;
		su_get_midpoint(ap->traces.a[0], &m0x, &m0y);
		float s = semblance_2d(ap, 0, 0, C, t0s, m0x, m0y, &_stack);
		if (s > smax) {
			smax = s;
			Copt = C;
			*stack = _stack;
		}
	}
	if (sem)
		*sem = smax;
	return Copt;
}

int main(int argc, char *argv[])
{
	if (argc != 9) {
		fprintf(stderr, "Usage: %s C0 C1 NC APH TAU INPUT CDP0 CDP1\n", argv[0]);
		exit(1);
	}

	float c0 = atof(argv[1]);
	float c1 = atof(argv[2]);
	int nc = atoi(argv[3]);
	int aph = atoi(argv[4]);
	float tau = strtof(argv[5], NULL);
	char *path = argv[6];
	int cdp0 = atoi(argv[7]);
	int cdp1 = atoi(argv[8]);
	FILE *fp = fopen(path, "r");

	su_trace_t tr;
	struct cdp_traces *cdp_traces = NULL;
	while (su_fgettr(fp, &tr)) {
		float hx, hy;
		su_get_halfoffset(&tr, &hx, &hy);
		if (hx*hx + hy*hy > aph*aph)
			continue;
		int cdp = tr.cdp;
		struct cdp_traces *val;
		HASH_FIND_INT(cdp_traces, &cdp, val);
		if (!val) {
			val = malloc(sizeof(struct cdp_traces));
			val->cdp = cdp;
			vector_init(val->traces);
			HASH_ADD_INT(cdp_traces, cdp, val);
		}
		vector_push(val->traces, tr);
	}

	FILE *C_out = fopen("c.su", "w");
	FILE *S_out = fopen("cmp.coher.su", "w");
	FILE *stack = fopen("cmp.stack.su", "w");
	float progress_max = 1.0f / (HASH_COUNT(cdp_traces) - 1);
	int k = 0;

	struct cdp_traces *iter;
	for (iter = cdp_traces; iter; iter = iter->hh.next) {
		if (iter->cdp < cdp0 || iter->cdp > cdp1)
			continue;
		su_trace_t *trs = iter->traces.a;
		su_trace_t ctr, str, stacktr;

		memcpy(&ctr, &trs[0], SU_HEADER_SIZE);
		ctr.offset = 0;
		ctr.sx = ctr.gx = (trs[0].sx + trs[0].gx) / 2;
		ctr.sy = ctr.gy = (trs[0].sy + trs[0].gy) / 2;

		memcpy(&str, &ctr, SU_HEADER_SIZE);
		memcpy(&stacktr, &ctr, SU_HEADER_SIZE);
		su_init(&ctr);
		su_init(&str);
		su_init(&stacktr);

		aperture_t ap;
		ap.ap_m = 0;
		ap.ap_h = aph;
		ap.ap_t = tau;
		vector_init(ap.traces);
		for (int i = 0; i < iter->traces.len; i++)
			vector_push(ap.traces, &vector_get(iter->traces, i));

		#pragma omp parallel for
		for (int t0 = 0; t0 < trs[0].ns; t0++) {
			float sem, stk;
			float C = getmax_C(&ap, t0, c0, c1, nc, &sem, &stk);
			ctr.data[t0] = C;
			str.data[t0] = sem;
			stacktr.data[t0] = stk;
		}
		su_fputtr(C_out, &ctr);
		su_fputtr(S_out, &str);
		su_fputtr(stack, &stacktr);
		log_progress(++k * progress_max, "Processing CDP %d", ctr.cdp);
	}
	printf("\n");

	return 0;
}
