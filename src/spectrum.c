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
#if PG_VERSION_NUM >= 160000
#include <varatt.h>
#endif
#include <fmgr.h>
#include <common/shortest_dec.h>
#include <utils/builtins.h>
#include <utils/float.h>
#include "spectrum.h"


static int spectrum_peak_cmp(const void *l, const void *r)
{
    SpectrumPeak *l_value = (SpectrumPeak *) l;
    SpectrumPeak *r_value = (SpectrumPeak *) r;
    return l_value->mz == r_value->mz ? 0 : (l_value->mz < r_value->mz ? -1 : 1);
}


Datum create_spectrum(SpectrumPeak *data, int count)
{
    qsort(data, count, sizeof(SpectrumPeak), spectrum_peak_cmp);

    size_t size = 2 * count * sizeof(float4) + VARHDRSZ;

    void *result = palloc0(size);
    float4 *result_data = (float4*) VARDATA(result);
    SET_VARSIZE(result, size);

    for(size_t i = 0; i < count; i++)
    {
        result_data[i] = data[i].mz;
        result_data[count + i] = data[i].intenzity;
    }

    PG_RETURN_POINTER(result);
}


static void skip_blank(char **data)
{
    while(**data != '\0' && isspace((unsigned char) **data))
        (*data)++;
}


static float4 read_float(char **data)
{
    char *num = *data;

    while(*num != '\0' && isspace((unsigned char) *num))
        num++;

    if(*num == '\0')
        ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum literal")));

    errno = 0;

    float4 val = strtof(num, data);

    if(*data == num || errno != 0)
        ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum literal")));

    return val;
}


PG_FUNCTION_INFO_V1(spectrum_input);
Datum spectrum_input(PG_FUNCTION_ARGS)
{
    char *data = PG_GETARG_CSTRING(0);

    skip_blank(&data);

    if(*data == '[' || *data == '{')
    {
        char begin_char = *(data++);
        char end_char = begin_char == '[' ? ']' : '}';

        size_t count = 0;

        for(char *c = data; *c != '\0'; c++)
            if(*c == begin_char)
                count++;

        SpectrumPeak *peaks = palloc(count * sizeof(SpectrumPeak));

        for(size_t i = 0; i < count; i++)
        {
            if(i > 0)
            {
                skip_blank(&data);

                if(*data++ != ',')
                    ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum literal")));
            }

            skip_blank(&data);

            if(*data++ != begin_char)
                ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum literal")));

            peaks[i].mz = read_float(&data);
            skip_blank(&data);

            if(*data++ != ',')
                ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum literal")));

            peaks[i].intenzity = read_float(&data);
            skip_blank(&data);

            if(*data++ != end_char)
                ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum literal")));
        }

        skip_blank(&data);

        if(*data++ != end_char)
            ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum literal")));

        if(*data != '\0')
            ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum literal")));

        PG_RETURN_DATUM(create_spectrum(peaks, count));
    }
    else
    {
        size_t count = 0;

        for(char *c = data; *c != '\0'; c++)
            if(*c == ':')
                count++;

        SpectrumPeak *peaks = palloc(count * sizeof(SpectrumPeak));

        for(size_t i = 0; i < count; i++)
        {
            peaks[i].mz = read_float(&data);
            skip_blank(&data);

            if(*data++ != ':')
                ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum literal")));

            peaks[i].intenzity = read_float(&data);

            if(isspace((unsigned char) *data))
                skip_blank(&data);
            else if(i != count - 1 && *data != ',' && *data != ';')
                ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum literal")));

            if(i != count - 1 && (*data == ',' || *data == ';'))
                data++;
        }

        if(*data != '\0')
            ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum literal")));

        PG_RETURN_DATUM(create_spectrum(peaks, count));
    }
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
        *(buffer++) = ' ';
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


PG_FUNCTION_INFO_V1(spectrum_is_equal_to);
Datum spectrum_is_equal_to(PG_FUNCTION_ARGS)
{
    PG_RETURN_DATUM(DirectFunctionCall2(byteaeq, PG_GETARG_DATUM(0), PG_GETARG_DATUM(1)));
}


PG_FUNCTION_INFO_V1(spectrum_is_not_equal_to);
Datum spectrum_is_not_equal_to(PG_FUNCTION_ARGS)
{
    PG_RETURN_DATUM(DirectFunctionCall2(byteane, PG_GETARG_DATUM(0), PG_GETARG_DATUM(1)));
}


PG_FUNCTION_INFO_V1(spectrum_is_less_than);
Datum spectrum_is_less_than(PG_FUNCTION_ARGS)
{
    PG_RETURN_DATUM(DirectFunctionCall2(bytealt, PG_GETARG_DATUM(0), PG_GETARG_DATUM(1)));
}


PG_FUNCTION_INFO_V1(spectrum_is_greater_than);
Datum spectrum_is_greater_than(PG_FUNCTION_ARGS)
{
    PG_RETURN_DATUM(DirectFunctionCall2(byteagt, PG_GETARG_DATUM(0), PG_GETARG_DATUM(1)));
}


PG_FUNCTION_INFO_V1(spectrum_is_not_less_than);
Datum spectrum_is_not_less_than(PG_FUNCTION_ARGS)
{
    PG_RETURN_DATUM(DirectFunctionCall2(byteage, PG_GETARG_DATUM(0), PG_GETARG_DATUM(1)));
}


PG_FUNCTION_INFO_V1(spectrum_is_not_greater_than);
Datum spectrum_is_not_greater_than(PG_FUNCTION_ARGS)
{
    PG_RETURN_DATUM(DirectFunctionCall2(byteale, PG_GETARG_DATUM(0), PG_GETARG_DATUM(1)));
}


PG_FUNCTION_INFO_V1(spectrum_compare);
Datum spectrum_compare(PG_FUNCTION_ARGS)
{
    PG_RETURN_DATUM(DirectFunctionCall2(byteacmp, PG_GETARG_DATUM(0), PG_GETARG_DATUM(1)));
}
