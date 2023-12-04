/*
 * This file is part of the PGMS PostgreSQL extension distribution
 * available at https://bioinfo.uochb.cas.cz/gitlab/chemdb/pgms.
 *
 * Copyright (c) 2022 Marek Mosna
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
#include <funcapi.h>
#include <miscadmin.h>
#include <utils/builtins.h>
#include <utils/typcache.h>
#include <utils/lsyscache.h>
#include <utils/rangetypes.h>
#include "pgms.h"
#include "spectrum.h"
#include "import/input.h"
#include "import/return.h"


#define BEGIN_IONS_STR  "BEGIN IONS"
#define END_IONS_STR    "END IONS"
#define CHARGE_STR      "CHARGE"
#define PEPMASS_STR     "PEPMASS"
#define COMMENT_STR     "#;!/"
#define NEGATIVE_SIGN   '-'
#define POSITIVE_SIGN   '+'


static int read_line(Input *input, StringInfo buffer)
{
    input_read_line(input, buffer);

    int c = input_getc(input);

    while(c == '\n' || c == '\r')
        c = input_getc(input);

    if(c != EOF)
        input_ungetc(input, c);

    return buffer->len;
}


static bool is_comment(StringInfo str)
{
    size_t comments_lenght = sizeof(COMMENT_STR) - 1;

    for(size_t i = 0; i < comments_lenght; ++i)
        if(str->data[0] == COMMENT_STR[i])
            return true;

    return false;
}


static void set_parameter(ReturnTypeMetadata *meta, int charge_idx, int pepmass_idx, int pepintensity_idx, Datum *values, bool *isnull, char *name, int name_length, char *value)
{
    if(pepintensity_idx >= 0 && !pg_strncasecmp(PEPMASS_STR, name, name_length))
    {
        char *begin = value;

        while(isspace(*begin))
            begin++;

        while(*begin != '\0' && !isspace(*begin))
            begin++;

        while(isspace(*begin))
            begin++;

        if(*begin != '\0')
        {
            values[pepintensity_idx] = InputFunctionCall(&meta->attinfuncs[pepintensity_idx], begin, meta->attioparams[pepintensity_idx], meta->atttypmods[pepintensity_idx]);
            isnull[pepintensity_idx] = false;
        }
    }


    int idx = find_attribute(meta, name, name_length);

    if(idx < 0 || meta->attbasetypids[idx] == spectrumOid)
        return;

    if(idx == charge_idx && meta->attbasetypids[idx] != VARCHAROID && meta->attbasetypids[idx] != TEXTOID)
    {
        char *begin = value;

        while(isspace(*begin))
            begin++;

        char *end = begin;

        for(char *v = begin; *v != '\0'; v++)
            if(!isspace(*v))
                end = v;

        if(*end == POSITIVE_SIGN || *end == NEGATIVE_SIGN)
        {
            char sign = *end;

            for(char *v = end; v != begin; v--)
                *v = *(v-1);

            *begin = sign;
        }
    }
    else if(idx == pepmass_idx && meta->attbasetypids[idx] != VARCHAROID && meta->attbasetypids[idx] != TEXTOID)
    {
        char *end = value;

        while(isspace(*end))
            end++;

        while(*end != '\0' && !isspace(*end))
            end++;

        while(isspace(*end))
            end++;

        if(*end != '\0')
            *(end - 1) = '\0';
    }

    values[idx] = InputFunctionCall(&meta->attinfuncs[idx], value, meta->attioparams[idx], meta->atttypmods[idx]);
    isnull[idx] = false;
}


static void parse_global(Input *input, ReturnTypeMetadata *meta, int charge_idx, int pepmass_idx, int pepintensity_idx, Datum *values, bool *isnull)
{
    StringInfo line = makeStringInfo();
    read_line(input, line);

    while(strcmp(BEGIN_IONS_STR,line->data))
    {
        if(!is_comment(line))
        {
            char *separator = strchr(line->data, '=');

            if(separator == NULL)
                ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed parameter")));

            set_parameter(meta, charge_idx, pepmass_idx, pepintensity_idx, values, isnull, line->data, separator - line->data, separator + 1);
        }

        read_line(input, line);
    }

    pfree(line->data);
    pfree(line);
}


static HeapTuple parser_record(Input *input, ReturnTypeMetadata *meta, int charge_idx, int pepmass_idx, int pepintensity_idx, Datum *gvalues, bool *gisnull, bool read_begin)
{
    Datum *values = palloc(meta->tupdesc->natts * sizeof(Datum));
    bool *isnull = palloc(meta->tupdesc->natts * sizeof(bool));

    for(int i = 0; i < meta->tupdesc->natts; i++)
    {
        values[i] = gvalues[i];
        isnull[i] = gisnull[i];
    }


    StringInfo line = makeStringInfo();
    read_line(input, line);

    if(read_begin)
    {
        if(strcmp(BEGIN_IONS_STR,line->data))
            ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("unexpected line")));

        read_line(input, line);
    }


    char *separator;

    while((separator = strchr(line->data, '=')) != NULL)
    {
        set_parameter(meta, charge_idx, pepmass_idx, pepintensity_idx, values, isnull, line->data, separator - line->data, separator + 1);
        read_line(input, line);
    }


    StringInfo peaks = makeStringInfo();

    while(strcmp(END_IONS_STR,line->data))
    {
        char *end1 = NULL;
        char *end2 = NULL;

        float f1 = strtof(line->data, &end1);
        float f2 = strtof(end1, &end2);

        if(end1 == end2 || end2 != line->data + line->len)
            ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("malformed spectrum")));

        appendBinaryStringInfo(peaks, (void *) (&(SpectrumPeak){ f1, f2 }), sizeof(SpectrumPeak));
        read_line(input, line);
    }

    Datum spectrum = create_spectrum((SpectrumPeak *) peaks->data, peaks->len / sizeof(SpectrumPeak));

    for(int idx = 0; idx < meta->tupdesc->natts; idx++)
    {
        if(meta->attbasetypids[idx] == spectrumOid)
        {
            values[idx] = spectrum;
            isnull[idx] = false;

            if(TupleDescAttr(meta->tupdesc, idx)->atttypid != spectrumOid)
                domain_check(values[idx], isnull[idx], TupleDescAttr(meta->tupdesc, idx)->atttypid, NULL, NULL);
        }
    }

    return heap_form_tuple(meta->tupdesc, values, isnull);
}


static Datum populate_record_worker(FunctionCallInfo fcinfo, const char *funcname, bool have_record_arg)
{
    int value_arg_num = have_record_arg ? 1 : 0;
    int intensityfield_arg_num = have_record_arg ? 2 : 1;


    ReturnTypeMetadata *meta = get_return_type_metadata(fcinfo, funcname, have_record_arg);

    if(PG_ARGISNULL(value_arg_num))
    {
        if(have_record_arg && !PG_ARGISNULL(0))
            PG_RETURN_DATUM(PG_GETARG_DATUM(0));
        else
            PG_RETURN_NULL();
    }


    int charge_idx = find_attribute(meta, CHARGE_STR, sizeof(CHARGE_STR) - 1);
    int pepmass_idx = find_attribute(meta, PEPMASS_STR, sizeof(PEPMASS_STR) - 1);
    int pepintensity_idx = -1;

    if(!PG_ARGISNULL(intensityfield_arg_num))
    {
        VarChar *value = PG_GETARG_VARCHAR_PP(intensityfield_arg_num);
        char *field = VARDATA_ANY(value);
        int length = VARSIZE_ANY_EXHDR(value);
        pepintensity_idx = find_attribute(meta, field, length);
    }


    Datum *values = (Datum *) palloc(meta->tupdesc->natts * sizeof(Datum));
    bool *nulls = (bool *) palloc(meta->tupdesc->natts * sizeof(bool));

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
        heap_deform_tuple(&tuple, meta->tupdesc, values, nulls);
    }
    else
    {
        for(int i = 0; i < meta->tupdesc->natts; i++)
        {
            values[i] = 0;
            nulls[i] = true;
        }
    }


    Input *input = input_open_varchar(PG_GETARG_VARCHAR_P(value_arg_num));


    Datum result;

    PG_TRY();
    {
        parse_global(input, meta, charge_idx, pepmass_idx, pepintensity_idx, values, nulls);

        HeapTuple tuple = parser_record(input, meta, charge_idx, pepmass_idx, pepintensity_idx, values, nulls, false);
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
    int intensityfield_arg_num = have_record_arg ? 2 : 1;


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


    int charge_idx = find_attribute(meta, CHARGE_STR, sizeof(CHARGE_STR) - 1);
    int pepmass_idx = find_attribute(meta, PEPMASS_STR, sizeof(PEPMASS_STR) - 1);
    int pepintensity_idx = -1;

    if(!PG_ARGISNULL(intensityfield_arg_num))
    {
        VarChar *value = PG_GETARG_VARCHAR_PP(intensityfield_arg_num);
        char *field = VARDATA_ANY(value);
        int length = VARSIZE_ANY_EXHDR(value);
        pepintensity_idx = find_attribute(meta, field, length);
    }


    Datum *values = (Datum *) palloc(meta->tupdesc->natts * sizeof(Datum));
    bool *nulls = (bool *) palloc(meta->tupdesc->natts * sizeof(bool));

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
        heap_deform_tuple(&tuple, meta->tupdesc, values, nulls);
    }
    else
    {
        for(int i = 0; i < meta->tupdesc->natts; i++)
        {
            values[i] = 0;
            nulls[i] = true;
        }
    }


    MemoryContext tmp_cxt = AllocSetContextCreate(CurrentMemoryContext, "mgf temporary cxt", ALLOCSET_DEFAULT_SIZES);
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
        parse_global(input, meta, charge_idx, pepmass_idx, pepintensity_idx, values, nulls);

        void *extra = NULL;
        bool first;

        while(!input_eof(input))
        {
            /* use the tmp context so we can clean up after each tuple is done */
            MemoryContext old_cxt = MemoryContextSwitchTo(tmp_cxt);

            HeapTuple tuple = parser_record(input, meta, charge_idx, pepmass_idx, pepintensity_idx, values, nulls, tuplestore_tuple_count(tuple_store) > 0);

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


PG_FUNCTION_INFO_V1(mgf_to_record);
Datum mgf_to_record(PG_FUNCTION_ARGS)
{
    return populate_record_worker(fcinfo, "mgf_to_record", false);
}


PG_FUNCTION_INFO_V1(mgf_populate_record);
Datum mgf_populate_record(PG_FUNCTION_ARGS)
{
    return populate_record_worker(fcinfo, "mgf_populate_record", true);
}


PG_FUNCTION_INFO_V1(mgf_to_recordset);
Datum mgf_to_recordset(PG_FUNCTION_ARGS)
{
    return populate_recordset_worker(fcinfo, "mgf_to_recordset", false);
}


PG_FUNCTION_INFO_V1(mgf_populate_recordset);
Datum mgf_populate_recordset(PG_FUNCTION_ARGS)
{
    return populate_recordset_worker(fcinfo, "mgf_populate_recordset", true);
}
