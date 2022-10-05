/*
 * This file is part of the PGMS PostgreSQL extension distribution
 * available at https://bioinfo.uochb.cas.cz/gitlab/chemdb/pgms.
 *
 * Copyright (c) 2021-2022 Jakub Galgonek
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
#include <common/shortest_dec.h>
#include <errno.h>


PG_FUNCTION_INFO_V1(spectrum_input);
Datum spectrum_input(PG_FUNCTION_ARGS)
{
    char *data = PG_GETARG_CSTRING(0);

    size_t count = 0;

    for(char *buffer = data; *buffer != '\0'; buffer++)
        if(*buffer == ':')
            count++;

    size_t size = count * 2 * sizeof(float4) + VARHDRSZ;
    void *result = palloc0(size);
    SET_VARSIZE(result, size);

    float4 *values = (float4 *) VARDATA(result);
    char *buffer = data;

    for(size_t i = 0; i < count; i++)
    {
        for(int j = 0; j < 2; j++)
        {
            char *end;
            errno = 0;
            values[j * count + i] = strtof(buffer, &end);

            if(errno != 0 || *end != (j == 0 ? ':' : i == count - 1 ? '\0' : ','))
                ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum literal")));

            buffer = end + 1;
        }
    }

    PG_RETURN_POINTER(result);
}


PG_FUNCTION_INFO_V1(spectrum_output);
Datum spectrum_output(PG_FUNCTION_ARGS)
{
    void *spectrum = PG_DETOAST_DATUM(PG_GETARG_DATUM(0));

    size_t count = (VARSIZE(spectrum) - VARHDRSZ) / sizeof(float4) / 2;
    float4 *values = (float4 *) VARDATA(spectrum);

    char *result = (char *) palloc0(count ? 2 * count * (FLOAT_SHORTEST_DECIMAL_LEN + 1) : 1);
    char *buffer = result;

    for(size_t i = 0; i < count; i++)
    {
        buffer += float_to_shortest_decimal_bufn(values[i], buffer);
        *(buffer++) = ':';
        buffer += float_to_shortest_decimal_bufn(values[count + i], buffer);
        *(buffer++) = ',';
    }

    *(count ? buffer - 1 : buffer) = '\0';

    PG_FREE_IF_COPY(spectrum, 0);
    PG_RETURN_CSTRING(result);
}


PG_FUNCTION_INFO_V1(spectrum_max_intensity);
Datum spectrum_max_intensity(PG_FUNCTION_ARGS)
{
    void *spectrum = PG_DETOAST_DATUM(PG_GETARG_DATUM(0));

    size_t size = (VARSIZE(spectrum) - VARHDRSZ) / sizeof(float4);
    size_t count = size / 2;
    float4 *values = (float4 *) VARDATA(spectrum);
    float4 max = 0.0f;

    for(size_t i = count; i < size; i++)
        if(max < values[i])
            max = values[i];

    PG_RETURN_FLOAT4(max);
}


PG_FUNCTION_INFO_V1(spectrum_normalize);
Datum spectrum_normalize(PG_FUNCTION_ARGS)
{
    void *spectrum = PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
    size_t size = (VARSIZE(spectrum) - VARHDRSZ) / sizeof(float4);
    size_t count = size / 2;
    float4 *values = (float4 *) VARDATA(spectrum);

    float4 max = DatumGetFloat4(DirectFunctionCall1(spectrum_max_intensity, PG_GETARG_DATUM(0)));

    Datum result = (Datum) palloc0(VARSIZE(spectrum));
    float4 *result_values = (float4 *) VARDATA(result);
    SET_VARSIZE(result, VARSIZE(spectrum));

    for(size_t i = 0; i < count; i++)
    {
        result_values[i] = values[i];
        result_values[count + i] = values[count + i] / max;
    }

    PG_RETURN_DATUM(result);
}
