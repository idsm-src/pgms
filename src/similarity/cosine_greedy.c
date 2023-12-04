/*
 * This file is part of the PGMS PostgreSQL extension distribution
 * available at https://bioinfo.uochb.cas.cz/gitlab/chemdb/pgms.
 *
 * It is based on cosine similarities from the matchms library
 * available at https://github.com/matchms/matchms.
 *
 * Copyright (c) 2021-2022 Jakub Galgonek
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <postgres.h>
#if PG_VERSION_NUM >= 160000
#include <varatt.h>
#endif
#include <fmgr.h>
#include <math.h>
#include "similarity/cosine.h"


PG_FUNCTION_INFO_V1(cosine_greedy);
Datum cosine_greedy(PG_FUNCTION_ARGS)
{
    void *spec1 = PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
    void *spec2 = PG_DETOAST_DATUM(PG_GETARG_DATUM(1));

    int len1 = (VARSIZE(spec1) - VARHDRSZ) / sizeof(float4) / 2;
    float *restrict mz1 = (float4 *) VARDATA(spec1);
    float *restrict intensities1 = mz1 + len1;

    int len2 = (VARSIZE(spec2) - VARHDRSZ) / sizeof(float4) / 2;
    float *restrict mz2 = (float4 *) VARDATA(spec2);
    float *restrict intensities2 = mz2 + len2;

    float tolerance = PG_GETARG_FLOAT4(2);
    float mz_power = PG_GETARG_FLOAT4(3);
    float intensity_power = PG_GETARG_FLOAT4(4);


    int matches = 0;
    int lowest_idx = 0;
    float score = 0;

    for(int peak1 = 0; peak1 < len1; peak1++)
    {
        float low_bound = mz1[peak1] - tolerance;
        float high_bound = mz1[peak1] + tolerance;

        for(int peak2 = lowest_idx; peak2 < len2; peak2++)
        {
            if(mz2[peak2] > high_bound)
                break;

            lowest_idx = peak2 + 1;

            if(mz2[peak2] < low_bound)
                continue;

            matches++;
            score += calc_score(intensities1[peak1], intensities2[peak2], mz1[peak1], mz2[peak2], intensity_power, mz_power);
            break;
        }
    }

    if(score !=0)
    {
        float norm1 = calc_norm(intensities1, mz1, len1, intensity_power, mz_power);
        float norm2 = calc_norm(intensities2, mz2, len2, intensity_power, mz_power);

        score /= sqrtf(norm1 * norm2);
    }

    PG_FREE_IF_COPY(spec1, 0);
    PG_FREE_IF_COPY(spec2, 1);

    if(isfinite(score) && score < 0)
        score = 0;
    else if(isfinite(score) && score > 1)
        score = 1;
    else if(!isfinite(score))
        score = NAN;

    PG_RETURN_FLOAT4(score);
}


PG_FUNCTION_INFO_V1(cosine_greedy_simple);
Datum cosine_greedy_simple(PG_FUNCTION_ARGS)
{
    void *spec1 = PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
    void *spec2 = PG_DETOAST_DATUM(PG_GETARG_DATUM(1));

    int len1 = (VARSIZE(spec1) - VARHDRSZ) / sizeof(float4) / 2;
    float *restrict mz1 = (float4 *) VARDATA(spec1);
    float *restrict intensities1 = mz1 + len1;

    int len2 = (VARSIZE(spec2) - VARHDRSZ) / sizeof(float4) / 2;
    float *restrict mz2 = (float4 *) VARDATA(spec2);
    float *restrict intensities2 = mz2 + len2;

    float tolerance = PG_GETARG_FLOAT4(2);


    float score = 0;
    int matches = 0;
    int lowest_idx = 0;

    for(int peak1 = 0; peak1 < len1; peak1++)
    {
        float low_bound = mz1[peak1] - tolerance;
        float high_bound = mz1[peak1] + tolerance;

        for(int peak2 = lowest_idx; peak2 < len2; peak2++)
        {
            if(mz2[peak2] > high_bound)
                break;

            lowest_idx = peak2 + 1;

            if(mz2[peak2] < low_bound)
                continue;

            matches++;
            score += intensities1[peak1] * intensities2[peak2];
            break;
        }
    }

    if(score !=0)
    {
        float norm1 = calc_simple_norm(intensities1, len1);
        float norm2 = calc_simple_norm(intensities2, len2);

        score /= sqrtf(norm1 * norm2);
    }

    PG_FREE_IF_COPY(spec1, 0);
    PG_FREE_IF_COPY(spec2, 1);

    if(isfinite(score) && score < 0)
        score = 0;
    else if(isfinite(score) && score > 1)
        score = 1;
    else if(!isfinite(score))
        score = NAN;

    PG_RETURN_FLOAT4(score);
}
