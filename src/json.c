/*
 * This file is part of the PGMS PostgreSQL extension distribution
 * available at https://bioinfo.uochb.cas.cz/gitlab/chemdb/pgms.
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
#include <utils/builtins.h>
#include <catalog/pg_type.h>
#if PG_VERSION_NUM >= 130000
#include <utils/jsonb.h>
#else
#include <utils/jsonapi.h>
#endif

#include "pgms.h"

#define CVECTOR_CUSTOM_MALLOC
#define cvector_clib_free       pfree
#define cvector_clib_malloc     palloc
#define cvector_clib_realloc    repalloc
#define CVECTOR_LOGARITHMIC_GROWTH

#include "cvector.h"


typedef struct
{
    JsonbIterator *it;
    JsonbIteratorToken state;
    Datum *values;
    bool *isnull;
    size_t cnt;
}
json_ctx_t;


static void json_ctx_free(json_ctx_t *ctx)
{
    if(ctx == NULL)
        return;

    if(ctx->values)
        pfree(ctx->values);

    if(ctx->isnull)
        pfree(ctx->isnull);

    pfree(ctx);
}


static json_ctx_t *json_ctx_create(Jsonb *jb, TupleDesc td)
{
    json_ctx_t *json_ctx = (json_ctx_t *) palloc0(sizeof(json_ctx_t));

    json_ctx->it = JsonbIteratorInit(&jb->root);
    json_ctx->values = palloc(td->natts * sizeof(Datum));
    json_ctx->isnull = palloc(td->natts * sizeof(bool));

    if(JB_ROOT_IS_OBJECT(jb))
        json_ctx->cnt = 1;
    else if(JB_ROOT_IS_ARRAY(jb))
        json_ctx->cnt = JB_ROOT_COUNT(jb);
    else
        json_ctx->cnt = 0;

    return json_ctx;
}


void json_ctx_next(AttInMetadata *attinmeta, json_ctx_t *json_ctx)
{
    StringInfo data = makeStringInfo();
    TupleDesc tupdesc = attinmeta->tupdesc;
    Index idx = tupdesc->natts;
    size_t elem_cnt = 0;
    spectrum_t ion;
    cvector_vector_type(spectrum_t) spectrum = NULL;
    int ions_cnt = 0;

    do
    {
        JsonbValue val = { 0 };
        JsonbIteratorToken state = JsonbIteratorNext(&json_ctx->it, &val, false);

        switch(state)
        {
            case WJB_KEY:
                idx = 0;
                appendBinaryStringInfo(data, val.val.string.val, val.val.string.len);

                while(idx < tupdesc->natts && !TupleDescAttr(tupdesc, idx)->attisdropped
                        && strcmp(data->data, NameStr(TupleDescAttr(tupdesc, idx)->attname)))
                    idx++;
                break;

            case WJB_ELEM:
                if(tupdesc->attrs[idx].atttypid == spectrumOid)
                {
                    char *tmp = DatumGetCString(DirectFunctionCall1(numeric_out, NumericGetDatum(val.val.numeric)));
                    Datum result = DirectFunctionCall1(float4in, CStringGetDatum(tmp));

                    if(elem_cnt++ % 2 == 0)
                    {
                        ion.mz = DatumGetFloat4(result);
                        ion.intenzity = 0.0f;
                    }
                    else
                    {
                        ion.intenzity = DatumGetFloat4(result);
                        cvector_push_back(spectrum, ion);
                        ions_cnt--;
                    }

                    pfree(tmp);
                }
                break;

            case WJB_VALUE:
                if(idx != tupdesc->natts)
                {
                    if(val.type == jbvNumeric)
                        appendStringInfoString(data, DatumGetCString(DirectFunctionCall1(numeric_out, NumericGetDatum(val.val.numeric))));
                    else
                        appendBinaryStringInfo(data, val.val.string.val, val.val.string.len);

                    json_ctx->values[idx] = InputFunctionCall(&attinmeta->attinfuncs[idx], data->data, attinmeta->attioparams[idx], attinmeta->atttypmods[idx]);
                    json_ctx->isnull[idx] = false;
                }

                idx = tupdesc->natts;
                break;

            case WJB_BEGIN_ARRAY:
                if(idx != tupdesc->natts && tupdesc->attrs[idx].atttypid == spectrumOid && ions_cnt == 0)
                {
                    ions_cnt = json_ctx->it->nElems;
                    cvector_reserve(spectrum, ions_cnt);
                }

                if(idx == tupdesc->natts)
                    json_ctx->state = WJB_BEGIN_OBJECT;

                break;

            case WJB_END_ARRAY:
                if(idx != tupdesc->natts && tupdesc->attrs[idx].atttypid == spectrumOid && ions_cnt == 0)
                {
                    qsort(spectrum, cvector_size(spectrum), sizeof(spectrum_t), spectrum_cmp);
                    set_spectrum(attinmeta, json_ctx->values, json_ctx->isnull, spectrum, cvector_size(spectrum));
                    cvector_free(spectrum);
                    idx = tupdesc->natts;
                }

                break;

            case WJB_BEGIN_OBJECT:
                json_ctx->state = json_ctx->state == WJB_DONE ? WJB_BEGIN_OBJECT : WJB_END_OBJECT;
                break;

            case WJB_END_OBJECT:
                json_ctx->state = json_ctx->state == WJB_END_OBJECT ? WJB_BEGIN_OBJECT : WJB_DONE;
                break;

            case WJB_DONE:
            default:
                break;
        }

        resetStringInfo(data);
    }
    while(json_ctx->state != WJB_DONE);

    pfree(data->data);
    pfree(data);
}


PG_FUNCTION_INFO_V1(load_from_json);
Datum load_from_json(PG_FUNCTION_ARGS)
{
    FuncCallContext *funcctx = NULL;
    json_ctx_t *json_ctx = NULL;
    TupleDesc tuple_desc = NULL;

    if(SRF_IS_FIRSTCALL())
    {
        MemoryContext oldcontext = NULL;

        funcctx = SRF_FIRSTCALL_INIT();
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        if(get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("unsupported return type")));

        funcctx->attinmeta = TupleDescGetAttInMetadata(tuple_desc);
        funcctx->user_fctx = (void*) json_ctx_create(PG_GETARG_JSONB_P(0), tuple_desc);
        MemoryContextSwitchTo(oldcontext);
    }

    funcctx = SRF_PERCALL_SETUP();
    json_ctx = (json_ctx_t *) funcctx->user_fctx;
    tuple_desc = funcctx->attinmeta->tupdesc;

    PG_TRY();
    {
        for(Index i = 0; i < tuple_desc->natts; i++)
        {
            json_ctx->values[i] = (Datum) NULL;
            json_ctx->isnull[i] = true;
        }
        json_ctx_next(funcctx->attinmeta, json_ctx);
    }
    PG_CATCH();
    {
        json_ctx_free(json_ctx);
        PG_RE_THROW();
    }
    PG_END_TRY();

    if(json_ctx->cnt--)
    {
        HeapTuple tuple = heap_form_tuple(funcctx->attinmeta->tupdesc, json_ctx->values, json_ctx->isnull);
        SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
    }

    json_ctx_free(json_ctx);
    SRF_RETURN_DONE(funcctx);
}
