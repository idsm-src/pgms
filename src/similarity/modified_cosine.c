/*
 * This file is part of the PGMS PostgreSQL extension distribution
 * available at https://bioinfo.uochb.cas.cz/gitlab/chemdb/pgms.
 *
 * It is based on cosine similarities from the matchms library
 * available at https://github.com/matchms/matchms.
 *
 * Copyright (c) 2022 Marek Mosna
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


PG_FUNCTION_INFO_V1(modified_cosine);
Datum modified_cosine(PG_FUNCTION_ARGS)
{
    bytea *reference = PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
    bytea *query = PG_DETOAST_DATUM(PG_GETARG_DATUM(1));

    size_t reference_len = (VARSIZE(reference) - VARHDRSZ) / sizeof(float4) / 2;
    float4 *restrict reference_mzs = (float4 *) VARDATA(reference);
    float4 *restrict reference_peaks = reference_mzs + reference_len;

    size_t query_len = (VARSIZE(query) - VARHDRSZ) / sizeof(float4) / 2;
    float4 *restrict query_mzs = (float4 *) VARDATA(query);
    float4 *restrict query_peaks = query_mzs + query_len;

    float4 shift = PG_GETARG_FLOAT4(2);
    float4 tolerance = PG_GETARG_FLOAT4(3);
    float4 mz_power = PG_GETARG_FLOAT4(4);
    float4 intensity_power = PG_GETARG_FLOAT4(5);

    size_t matches = 0;
    Index lowest_idx = 0;
    float4 score = 0;

    for(Index reference_index = 0; reference_index < reference_len; reference_index++)
    {
        float4 low_bound = reference_mzs[reference_index] - tolerance;
        float4 high_bound = reference_mzs[reference_index] + tolerance;

        for(Index query_index = lowest_idx; query_index < query_len; query_index++)
        {
            if(Max(query_mzs[query_index], query_mzs[query_index] + shift) > high_bound)
                break;

            lowest_idx = query_index + 1;

            if(Min(query_mzs[query_index], query_mzs[query_index] + shift) < low_bound)
                continue;

            matches++;
            score += calc_score(reference_peaks[reference_index], query_peaks[query_index],
                    reference_mzs[reference_index], query_mzs[query_index], intensity_power, mz_power);

            break;
        }
    }

    if(score != 0.0f)
    {
        float4 norm1 = calc_norm(reference_peaks, reference_mzs, reference_len, intensity_power, mz_power);
        float4 norm2 = calc_norm(query_peaks, query_mzs, query_len, intensity_power, mz_power);

        score /= sqrtf(norm1 * norm2);
    }

    PG_FREE_IF_COPY(reference, 0);
    PG_FREE_IF_COPY(query, 1);

    if(isfinite(score) && score < 0)
        score = 0;
    else if(isfinite(score) && score > 1)
        score = 1;
    else if(!isfinite(score))
        score = NAN;

    PG_RETURN_FLOAT4(score);
}
