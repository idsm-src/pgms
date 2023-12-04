/*
 * This file is part of the PGMS PostgreSQL extension distribution
 * available at https://bioinfo.uochb.cas.cz/gitlab/chemdb/pgms.
 *
 * Copyright (c) 2022 Jakub Galgonek
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
#include <miscadmin.h>
#include <utils/builtins.h>
#include "pgms.h"
#include "spectrum.h"
#include "import/input.h"
#include "import/return.h"


#define RECORD_END  "$$$$"


static int read_line(Input *input, StringInfo buffer)
{
    if(input_eof(input))
        ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("unexpected end of input")));

    return input_read_line(input, buffer);
}


static HeapTuple parser_record(Input *input, ReturnTypeMetadata *meta, int molidx, Datum *default_values, bool *default_nulls)
{
    Datum *values = palloc(meta->tupdesc->natts * sizeof(Datum));
    bool *isnull = palloc(meta->tupdesc->natts * sizeof(bool));

    for(int i = 0; i < meta->tupdesc->natts; i++)
    {
        values[i] = default_values ? default_values[i] : 0;
        isnull[i] = default_nulls ? default_nulls[i] : true;
    }


    StringInfo value = makeStringInfo();
    StringInfo line = makeStringInfo();

    read_line(input, line);

    while(strncmp(line->data, ">  <", 4) && strcmp(line->data, RECORD_END))
    {
        appendBinaryStringInfo(value, line->data, line->len);
        appendStringInfoChar(value, '\n');
        read_line(input, line);
    }

    if(molidx >= 0)
    {
        values[molidx] = InputFunctionCall(&meta->attinfuncs[molidx], value->data, meta->attioparams[molidx], meta->atttypmods[molidx]);
        isnull[molidx] = false;
    }


    while(!strncmp(line->data, ">  <", 4) && line->data[line->len - 1] == '>')
    {
        int idx = find_attribute(meta, line->data + 4, line->len - 5);

        resetStringInfo(value);
        read_line(input, line);

        if(idx >= 0 && meta->attbasetypids[idx] == spectrumOid)
        {
            do
            {
                char *end1 = NULL;
                char *end2 = NULL;

                float f1 = strtof(line->data, &end1);
                float f2 = strtof(end1, &end2);

                if(end1 == end2 || end2 != line->data + line->len)
                    ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum")));

                appendBinaryStringInfo(value, (void *) (&(SpectrumPeak){ f1, f2 }), sizeof(SpectrumPeak));
            }
            while(read_line(input, line) > 0);

            values[idx] = create_spectrum((SpectrumPeak *) value->data, value->len / sizeof(SpectrumPeak));
            isnull[idx] = false;

            if(TupleDescAttr(meta->tupdesc, idx)->atttypid != spectrumOid)
                domain_check(values[idx], isnull[idx], TupleDescAttr(meta->tupdesc, idx)->atttypid, NULL, NULL);
        }
        else
        {
            do
            {
                appendBinaryStringInfo(value, line->data, line->len);
                appendStringInfoChar(value, '\n');
            }
            while(read_line(input, line) > 0);

            value->data[--value->len] = '\0';

            if(idx >= 0 && value->len)
            {
                values[idx] = InputFunctionCall(&meta->attinfuncs[idx], value->data, meta->attioparams[idx], meta->atttypmods[idx]);
                isnull[idx] = false;
            }
        }

        read_line(input, line);
    }

    if(strcmp(line->data, RECORD_END))
        ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("unexpected line")));

    if(!input_eof(input) && read_line(input, line) > 0)
        ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("unexpected line")));


    if(default_values == NULL)
    {
        for(int i = 0; i < meta->tupdesc->natts; i++)
            if(!TupleDescAttr(meta->tupdesc, i)->attisdropped)
                InputFunctionCall(&meta->attinfuncs[i], NULL, meta->attioparams[i], meta->atttypmods[i]);
    }

    return heap_form_tuple(meta->tupdesc, values, isnull);
}


static Datum populate_record_worker(FunctionCallInfo fcinfo, const char *funcname, bool have_record_arg)
{
    int value_arg_num = have_record_arg ? 1 : 0;
    int molfield_arg_num = have_record_arg ? 2 : 1;


    ReturnTypeMetadata *meta = get_return_type_metadata(fcinfo, funcname, have_record_arg);

    if(PG_ARGISNULL(value_arg_num))
    {
        if(have_record_arg && !PG_ARGISNULL(0))
            PG_RETURN_DATUM(PG_GETARG_DATUM(0));
        else
            PG_RETURN_NULL();
    }


    int molidx = -1;

    if(!PG_ARGISNULL(molfield_arg_num))
    {
        VarChar *value = PG_GETARG_VARCHAR_PP(molfield_arg_num);
        char *name = VARDATA_ANY(value);
        int length = VARSIZE_ANY_EXHDR(value);
        molidx = find_attribute(meta, name, length);
    }


    Datum *values = NULL;
    bool *nulls = NULL;

    if(have_record_arg && !PG_ARGISNULL(0))
    {
        HeapTupleHeader defaultval = PG_GETARG_HEAPTUPLEHEADER(0);

        /* build a temporary HeapTuple control structure */
        HeapTupleData tuple;
        tuple.t_len = HeapTupleHeaderGetDatumLength(defaultval);
        ItemPointerSetInvalid(&(tuple.t_self));
        tuple.t_tableOid = InvalidOid;
        tuple.t_data = defaultval;

        /* break down the tuple into fields */
        values = (Datum *) palloc(meta->tupdesc->natts * sizeof(Datum));
        nulls = (bool *) palloc(meta->tupdesc->natts * sizeof(bool));
        heap_deform_tuple(&tuple, meta->tupdesc, values, nulls);
    }


    Input *input = input_open_varchar(PG_GETARG_VARCHAR_P(value_arg_num));


    Datum result;

    PG_TRY();
    {
        HeapTuple tuple = parser_record(input, meta, molidx, values, nulls);
        result = HeapTupleHeaderGetDatum(tuple->t_data);

        /* call the "in" function for each non-dropped null attribute to support domains */
        if(!have_record_arg || PG_ARGISNULL(0))
            for(int i = 0; i < meta->tupdesc->natts; i++)
                if(!TupleDescAttr(meta->tupdesc, i)->attisdropped && nulls[i])
                    InputFunctionCall(&meta->attinfuncs[i], NULL, meta->attioparams[i], meta->atttypmods[i]);

        if(meta->typid != meta->tupdesc->tdtypeid)
            domain_check(result, false, meta->typid, NULL, NULL);

        if(!input_eof(input))
            ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("unexpected data at the end of the input")));
    }
    PG_FINALLY();
    {
        input_close(input);
    }
    PG_END_TRY();

    PG_RETURN_DATUM(result);
}


static Datum populate_recordset_worker(FunctionCallInfo fcinfo, const char *funcname, bool have_record_arg)
{
    int value_arg_num = have_record_arg ? 1 : 0;
    int molfield_arg_num = have_record_arg ? 2 : 1;


    ReturnSetInfo *rsi = (ReturnSetInfo *) fcinfo->resultinfo;

    if(!rsi || !IsA(rsi, ReturnSetInfo))
        ereport(ERROR,(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("set-valued function called in context that cannot accept a set")));

    if(!(rsi->allowedModes & SFRM_Materialize))
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("materialize mode required, but it is not allowed in this context")));

    rsi->returnMode = SFRM_Materialize;


    ReturnTypeMetadata *meta = get_return_type_metadata(fcinfo, funcname, have_record_arg);

    if(PG_ARGISNULL(value_arg_num))
        PG_RETURN_NULL();


    int molidx = -1;

    if(!PG_ARGISNULL(molfield_arg_num))
    {
        VarChar *value = PG_GETARG_VARCHAR_PP(molfield_arg_num);
        char *name = VARDATA_ANY(value);
        int length = VARSIZE_ANY_EXHDR(value);
        molidx = find_attribute(meta, name, length);
    }


    Datum *values = NULL;
    bool *nulls = NULL;

    if(have_record_arg && !PG_ARGISNULL(0))
    {
        HeapTupleHeader defaultval = PG_GETARG_HEAPTUPLEHEADER(0);

        /* build a temporary HeapTuple control structure */
        HeapTupleData tuple;
        tuple.t_len = HeapTupleHeaderGetDatumLength(defaultval);
        ItemPointerSetInvalid(&(tuple.t_self));
        tuple.t_tableOid = InvalidOid;
        tuple.t_data = defaultval;

        /* break down the tuple into fields */
        values = (Datum *) palloc(meta->tupdesc->natts * sizeof(Datum));
        nulls = (bool *) palloc(meta->tupdesc->natts * sizeof(bool));
        heap_deform_tuple(&tuple, meta->tupdesc, values, nulls);
    }


    MemoryContext tmp_cxt = AllocSetContextCreate(CurrentMemoryContext, "sdf temporary cxt", ALLOCSET_DEFAULT_SIZES);
    MemoryContext old_cxt = MemoryContextSwitchTo(rsi->econtext->ecxt_per_query_memory);
    Tuplestorestate *tuple_store = tuplestore_begin_heap(rsi->allowedModes & SFRM_Materialize_Random, false, work_mem);
    MemoryContextSwitchTo(old_cxt);


    Oid element_type = get_fn_expr_argtype(fcinfo->flinfo, value_arg_num);
    Input *input = NULL;

    if(element_type == VARCHAROID)
        input = input_open_varchar(PG_GETARG_VARCHAR_P(value_arg_num));
    else if(element_type == OIDOID)
        input = input_open_lo(PG_GETARG_OID(value_arg_num));
    else
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("unsupported argument type")));


    PG_TRY();
    {
        void *extra = NULL;

        while(!input_eof(input))
        {
            /* use the tmp context so we can clean up after each tuple is done */
            MemoryContext old_cxt = MemoryContextSwitchTo(tmp_cxt);

            HeapTuple tuple = parser_record(input, meta, molidx, values, nulls);

            /* call the "in" function for each non-dropped null attribute to support domains */
            if(!have_record_arg || PG_ARGISNULL(0))
                for(int i = 0; i < meta->tupdesc->natts; i++)
                    if(!TupleDescAttr(meta->tupdesc, i)->attisdropped && nulls[i])
                        InputFunctionCall(&meta->attinfuncs[i], NULL, meta->attioparams[i], meta->atttypmods[i]);

            if(meta->typid != meta->tupdesc->tdtypeid)
                domain_check(HeapTupleHeaderGetDatum(tuple->t_data), false, meta->typid, &extra, old_cxt);

            tuplestore_puttuple(tuple_store, tuple);

            /* clean up and switch back */
            MemoryContextSwitchTo(old_cxt);
            MemoryContextReset(tmp_cxt);
        }
    }
    PG_FINALLY();
    {
        input_close(input);
    }
    PG_END_TRY();

    rsi->setResult = tuple_store;
    rsi->setDesc = CreateTupleDescCopy(meta->tupdesc);


    MemoryContextDelete(tmp_cxt);
    PG_RETURN_NULL();
}


PG_FUNCTION_INFO_V1(sdf_to_record);
Datum sdf_to_record(PG_FUNCTION_ARGS)
{
    return populate_record_worker(fcinfo, "sdf_to_record", false);
}


PG_FUNCTION_INFO_V1(sdf_populate_record);
Datum sdf_populate_record(PG_FUNCTION_ARGS)
{
    return populate_record_worker(fcinfo, "sdf_populate_record", true);
}


PG_FUNCTION_INFO_V1(sdf_to_recordset);
Datum sdf_to_recordset(PG_FUNCTION_ARGS)
{
    return populate_recordset_worker(fcinfo, "sdf_to_recordset", false);
}


PG_FUNCTION_INFO_V1(sdf_populate_recordset);
Datum sdf_populate_recordset(PG_FUNCTION_ARGS)
{
    return populate_recordset_worker(fcinfo, "sdf_populate_recordset", true);
}
