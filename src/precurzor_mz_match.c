/*
 * This file is part of the PGMS PostgreSQL extension distribution
 * available at https://bioinfo.uochb.cas.cz/gitlab/chemdb/pgms.
 *
 * It is based on the precursor m/z similarity from the matchms library
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
#include <fmgr.h>
#include <math.h>
#include <float.h>

#define DALTON_TYPE "Dalton"
#define PPM_TYPE    "ppm"


PG_FUNCTION_INFO_V1(precurzor_mz_match);
Datum precurzor_mz_match(PG_FUNCTION_ARGS)
{
    const float query = PG_GETARG_FLOAT4(0);
    const float reference = PG_GETARG_FLOAT4(1);
    const float tolerance = PG_GETARG_FLOAT4(2);
    VarChar *tolerance_type = PG_GETARG_VARCHAR_P(3);

    bool match = false;
    float dif = abs(reference - query);

    if(!strcmp(VARDATA(tolerance_type), DALTON_TYPE))
        match = (dif <= tolerance);
    else
        match = (dif / (abs(reference + query) / 2) * 1e6 <= tolerance);

    PG_RETURN_FLOAT4(match ? 1.0f : 0.0f);
}
