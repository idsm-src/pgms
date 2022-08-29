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

#include "pgms.h"

#include <postgres.h>
#include <fmgr.h>
#include <catalog/namespace.h>
#include <catalog/pg_type.h>
#include <utils/syscache.h>
#include <utils/array.h>

PG_MODULE_MAGIC;


Oid spectrumOid;


void _PG_init()
{
    Oid spaceid = LookupExplicitNamespace("pgms", false);
    spectrumOid = GetSysCacheOid2(TYPENAMENSP, Anum_pg_type_oid, PointerGetDatum("spectrum"), ObjectIdGetDatum(spaceid));
}

char* reverse_postfix_sign(char* value)
{
    size_t length = 0;
    char *psign = NULL;
    char* pbegin = value;
    
    if(!value || *value == '\0')
        goto end;

    length = strlen(value);
    psign = value + length - 1;

    while(likely(pbegin != psign) && isspace((unsigned char) *pbegin))
        pbegin++;

    while(likely(pbegin != psign) && isspace((unsigned char) *psign))
        psign--;

    if(unlikely(psign == pbegin))
        goto end;
    else if(*psign == POSITIVE_SIGN)
        *psign = '\0';
    else if(*psign == NEGATIVE_SIGN)
    {
        while(psign != pbegin)
        {
            char c = *(psign - 1);
            *psign = c;
            *(--psign) = NEGATIVE_SIGN;
        }
    }

end:
    elog(DEBUG1, "rotated: %s", value);
    return value;
}

PG_FUNCTION_INFO_V1(precursor_mz_correction_float);
Datum precursor_mz_correction_float(PG_FUNCTION_ARGS)
{
    PG_RETURN_DATUM(PG_GETARG_DATUM(0));
}

PG_FUNCTION_INFO_V1(precursor_mz_correction_array);
Datum precursor_mz_correction_array(PG_FUNCTION_ARGS)
{
    ArrayType *array = PG_GETARG_ARRAYTYPE_P(0);

    PG_RETURN_DATUM(
        ARR_SIZE(array) > 0 
        ? Float4GetDatum(*(float4*)ARR_DATA_PTR(array))
        : 0
    );
}

void set_spectrum(AttInMetadata *attinmeta, Datum *values, bool *isnull, spectrum_t* data, int count)
{
    TupleDesc tupdesc = attinmeta->tupdesc;
    size_t size = count * sizeof(spectrum_t) + VARHDRSZ;
    void *result = palloc0(size);
    float4* result_data = (float4*) VARDATA(result);

    SET_VARSIZE(result, size);

    for(size_t i = 0; i < count; i++)
    {
        result_data[i] = data[i].mz;
        result_data[count + i] = data[i].intenzity;
    }

    for(int idx = 0; idx < tupdesc->natts; idx++)
    {
        if(tupdesc->attrs[idx].atttypid == spectrumOid)
        {
            values[idx] = PointerGetDatum(result);
            isnull[idx] = false;
        }
    }
}

int spectrum_cmp(const void* l,const void* r)
{
    spectrum_t* l_value = (spectrum_t*)l;
    spectrum_t* r_value = (spectrum_t*)r;
    return l_value->mz > r_value->mz;
}
