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

#include "pgms.h"

#include <funcapi.h>
#include <utils/builtins.h>
#include <utils/rangetypes.h>
#include <storage/large_object.h>
#include <libpq/libpq-fs.h>
#include <catalog/pg_type.h>

#include "pgms.h"

#define CVECTOR_CUSTOM_MALLOC
#define cvector_clib_free       pfree
#define cvector_clib_malloc     palloc
#define cvector_clib_realloc    repalloc
#define CVECTOR_LOGARITHMIC_GROWTH

#include "cvector.h"

#define BEGIN_IONS_STR          "BEGIN IONS"
#define END_IONS_STR            "END IONS"
#define CHARGE_STR              "CHARGE"
#define COMMENT_STR             "#;!/"
#define BUFFER_SIZE             (1 << 20) //1MB
#define INITIAL_SPECTRUM_SIZE   200

typedef enum 
{
    BEGIN       = 0,
    BEGIN_IONS  = 1,
    END_IONS    = 2,
    END         = 3,
}
ParserState;

typedef struct
{
    LargeObjectDesc*    file;
    int                 pos;
    int                 size;
    char*               data;
    Datum*              g_values;
    bool*               g_isnull;
    AttInMetadata *     meta;
    ParserState         status;
}
ParserData, *Parser;

static bool is_blank(const char* s)
{
    Assert(s);

    while(true)
    {
        if(isgraph(*s))
            return false;

        if(*s == '\0' || *s == '\n')
            return true;

        s++;
    }
}

static bool is_comment(StringInfo str)
{
    size_t comments_lenght = sizeof(COMMENT_STR) - 1;

    for(size_t i = 0; i < comments_lenght; ++i)
        if(str->data[0] == COMMENT_STR[i])
        {
            elog(DEBUG1, "Comment: %s", str->data + 1);
            return true;
        }

    return false;
}

static void set_parameter(AttInMetadata *attinmeta, Datum *values, bool *isnull, char *name, char *value)
{
    TupleDesc tupdesc = attinmeta->tupdesc;

    int idx = 0;

    while(idx < tupdesc->natts
        && !TupleDescAttr(tupdesc, idx)->attisdropped
        && strcmp(name, NameStr(TupleDescAttr(tupdesc, idx)->attname))
        )
        idx++;

    if(idx == tupdesc->natts || tupdesc->attrs[idx].atttypid == spectrumOid)
        return;

    Oid typid = TupleDescAttr(tupdesc, idx)->atttypid;

    elog(DEBUG1, "param %s with typeiod: %d", name, typid);

    if(typid == INT4RANGEOID || typid == INT8RANGEOID || typid == NUMRANGEOID)
    { // special treatment of mgf ranges
        char *ptr = value;

        while(*ptr != '\0' && isspace((unsigned char) *ptr))
            ptr++;

        if(*ptr != '(' && *ptr != '[' && strncmp(ptr, "empty", 5))
        {
            char *lbound = value;
            char *rbound = value;

            ptr = strchr(ptr + 1, '-');

            if(ptr != NULL)
            {
                lbound[ptr - value] = '\0';
                rbound = ptr + 1;
            }

            Datum (*in)(PG_FUNCTION_ARGS) = typid == NUMRANGEOID ? numeric_in : typid == INT4RANGEOID ? int4in : int8in;
            Datum lval = DirectFunctionCall3(in, CStringGetDatum(lbound), ObjectIdGetDatum(InvalidOid), Int32GetDatum(-1));
            Datum rval = DirectFunctionCall3(in, CStringGetDatum(rbound), ObjectIdGetDatum(InvalidOid), Int32GetDatum(-1));

            RangeBound lower = { .infinite = false, .inclusive = true, .lower = true, .val = lval };
            RangeBound upper = { .infinite = false, .inclusive = true, .lower = false, .val = rval };

            TypeCacheEntry *typcache = lookup_type_cache(typid, TYPECACHE_RANGE_INFO);
            values[idx] = RangeTypePGetDatum(make_range(typcache, &lower, &upper, false));
            isnull[idx] = false;

            return;
        }
    }
    else if(typid == INT4OID || typid == NUMERICOID || typid == INT8OID || typid == FLOAT4OID || typid == FLOAT8OID)
    { // special treatment of postfix sign
        reverse_postfix_sign(value);
    }
    else if(typid == FLOAT4ARRAYOID || typid == FLOAT8ARRAYOID || typid == INT4ARRAYOID || typid == INT8ARRAYOID ||typid == NUMERICARRAYOID)
    { // special treat for array of numbers
        cvector_vector_type(char*)  tkns = NULL;
        char                        *pbegin = value;
        char                        *pend = NULL;
        bool                        spacing = false;
        size_t                      count = 0;

        pend = value + strlen(value);

        while(likely(pbegin != pend) && isspace((unsigned char) *pbegin))
            pbegin++;

        while(likely(pbegin != pend) && isspace((unsigned char) *pend))
            pend--;

        if(unlikely(pend == pbegin))
        {
            values[idx] = (Datum)0;
            isnull[idx] = true;
            return;
        }

        cvector_reserve(tkns, 50);
        cvector_push_back(tkns, pbegin);

        for (value = pbegin; value != pend; value++)
        {
            if(unlikely(isspace(*value)))
            {
                spacing = true;
            }
            else if(unlikely(spacing))
            {
                *(value - 1) = '\0';
                cvector_push_back(tkns, value);
                spacing = false;
            }
        }

        StringInfo array = makeStringInfo();

        count = cvector_size(tkns);
        appendStringInfoCharMacro(array, '{');
        for(size_t i = 0; i < count; i++)
        {
            reverse_postfix_sign(tkns[i]);
            appendStringInfoString(array, tkns[i]);
            appendStringInfoCharMacro(array, i == count - 1 ? '}' : ',');
        }
        values[idx] = InputFunctionCall(&attinmeta->attinfuncs[idx], array->data, attinmeta->attioparams[idx], attinmeta->atttypmods[idx]);
        isnull[idx] = false;
        pfree(array->data);
        pfree(array);
        return;
    }

    values[idx] = InputFunctionCall(&attinmeta->attinfuncs[idx], value, attinmeta->attioparams[idx], attinmeta->atttypmods[idx]);
    isnull[idx] = false;
}

static Parser parser_init_lob(LargeObjectDesc* in)
{
    Parser parser = palloc(sizeof(ParserData));

    parser->pos = 0;
    parser->file = in;
    parser->size = 0;
    parser->data = palloc(BUFFER_SIZE);
    parser->g_values = NULL;
    parser->g_isnull = NULL;
    parser->meta = NULL;
    parser->status = BEGIN;

    return parser;
}

static Parser parser_init_varchar(VarChar* in)
{
    Parser parser = palloc(sizeof(ParserData));

    parser->pos = 0;
    parser->file = NULL;
    parser->size = VARSIZE(in) - VARHDRSZ;
    parser->data = palloc(parser->size);
    memcpy(parser->data, VARDATA(in), parser->size);
    parser->g_values = NULL;
    parser->g_isnull = NULL;
    parser->meta = NULL;
    parser->status = BEGIN;

    return parser;
}

static void parser_close(Parser parser)
{
    pfree(parser->data);

    if(parser->file)
    {
        close_lo_relation(true);
        inv_close(parser->file);
    }

    pfree(parser->g_values);
    pfree(parser->g_isnull);
    pfree(parser);
}

static int read_line(Parser parser, StringInfo buffer)
{
    int cnt = 0;
    bool white_only = true;

    do
    {
        char c = '\0';

        if(parser->pos == parser->size && parser->file != NULL)
        {
            parser->pos = 0;
            parser->size = inv_read(parser->file, parser->data, BUFFER_SIZE);
        }

        if(parser->pos == parser->size)
        {
            if(white_only)
                return EOF;
            else 
                break;
        }

        c = parser->data[parser->pos++];

        if(isgraph(c))
            white_only = false;
        else if (c == '\0')
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED)
                , errmsg("Unsupported character: '\\0'")));

        if(c == '\n')
            break;

        appendStringInfoCharMacro(buffer, c);

        cnt++;
    }
    while(true);

    elog(DEBUG1, "read line: %s", buffer->data);
    return white_only ? 0 : cnt;
}


static void parser_global_setup(Parser parser, AttInMetadata *meta)
{
    StringInfo line = makeStringInfo();

    Assert(meta);
    Assert(parser);

    parser->meta = meta;
    parser->g_values = palloc(meta->tupdesc->natts * sizeof(Datum));
    parser->g_isnull = palloc(meta->tupdesc->natts * sizeof(bool));

    for(int i = 0; i < meta->tupdesc->natts; i++)
    {
        parser->g_values[i] = (Datum) NULL;
        parser->g_isnull[i] = true;
    }

    while(true)
    {
        int result = read_line(parser, line);
        bool bcomment = false;
        char* eq = NULL;

        if(result == EOF)
            break;
        else if(result == 0)
            continue;

        bcomment = is_comment(line);

        if(!bcomment && parser->status == BEGIN && !strcmp (BEGIN_IONS_STR,line->data))
        {
            parser->status = BEGIN_IONS;
            break;
        }
        else if(!bcomment && parser->status == BEGIN &&((eq = strchr(line->data, '=')) != NULL))
        {
            *eq++ = '\0';
            set_parameter(parser->meta, parser->g_values, parser->g_isnull, line->data, eq);
            elog(DEBUG1, "%s: %s", line->data, eq);
        }
        resetStringInfo(line);
    }

    pfree(line->data);
    pfree(line);
}

static bool parser_next(Parser parser, Datum* l_values, bool* l_isnull)
{
    cvector_vector_type(spectrum_t) spectrum = NULL;
    StringInfo line = makeStringInfo();

    elog(DEBUG1, "parser_next");
    do
    {
        int result = 0;
        float f1, f2;
        char* pEnd1 = NULL;
        char* pEnd2 = NULL;
        char* eq = NULL;

        result = read_line(parser, line);

        if(result == EOF)
        {
            parser->status = END;
            break;
        }
        else if(result == 0)
        {
            resetStringInfo(line);
            continue;
        }

        if(is_comment(line))
        {
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED)
                , errmsg("Missplaced comment")));
        }
        else if(parser->status == BEGIN && !strcmp (BEGIN_IONS_STR,line->data))
        {
            elog(DEBUG1, BEGIN_IONS_STR);
            parser->status = BEGIN_IONS;
            cvector_reserve(spectrum, INITIAL_SPECTRUM_SIZE);
        }
        else if(parser->status == BEGIN_IONS && !strcmp (END_IONS_STR,line->data))
        {
            elog(DEBUG1, END_IONS_STR);
            qsort(spectrum, cvector_size(spectrum), sizeof(spectrum_t), spectrum_cmp);
            set_spectrum(parser->meta, l_values, l_isnull, spectrum, cvector_size(spectrum));
            cvector_free(spectrum);
            parser->status = END_IONS;
        }
        else if(parser->status == BEGIN_IONS && ((eq = strchr(line->data, '=')) != NULL))
        {
            *eq++ = '\0';
            set_parameter(parser->meta, l_values, l_isnull, line->data, eq);
            elog(DEBUG1, "%s: %s", line->data, eq);
        }
        else if(parser->status == BEGIN_IONS
            && ((f1 = strtof (line->data, &pEnd1)) || pEnd1 != line->data)
            && ((f2 = strtof (pEnd1, &pEnd2))      || pEnd2 != pEnd1) 
            )
        {
            if(!is_blank(pEnd2))
                ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED)
                    , errmsg("Unknown format line: %s", line->data)));

            *pEnd1++ = '\0';
            elog(DEBUG1, "[%s %s]", line->data, pEnd1);
            cvector_push_back(spectrum, ((spectrum_t){ f1, f2 }));
        }
        else
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED)
                , errmsg("Unknown format line: %s", line->data)));

        resetStringInfo(line);
    }
    while(parser->status < END_IONS);

    parser->status = parser->status != END ? BEGIN : END;
    pfree(line);
    return parser->status != END;
}

PG_FUNCTION_INFO_V1(load_from_mgf);
Datum load_from_mgf(PG_FUNCTION_ARGS)
{
    bool next;
    FuncCallContext *funcctx = NULL;
    Parser parser = NULL;
    Datum* values = NULL;
    bool* isnull = NULL;

    if(SRF_IS_FIRSTCALL())
    {
        MemoryContext oldcontext = NULL;
        TupleDesc tuple_desc = NULL;
        Oid element_type;
        AttInMetadata *attinmeta = NULL;

        funcctx = SRF_FIRSTCALL_INIT();
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        if(get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED)
                , errmsg("unsupported return type")));

        funcctx->attinmeta = TupleDescGetAttInMetadata(tuple_desc);
        element_type = get_fn_expr_argtype(fcinfo->flinfo, 0);

        if(element_type == VARCHAROID)
            parser = parser_init_varchar(PG_GETARG_VARCHAR_P(0));
        else if(element_type == OIDOID)
            parser = parser_init_lob(
                inv_open(PG_GETARG_OID(0), INV_READ, funcctx->multi_call_memory_ctx)
            );

        parser_global_setup(parser, funcctx->attinmeta);
        funcctx->user_fctx = (void*)parser;
        MemoryContextSwitchTo(oldcontext);
    }

    funcctx = SRF_PERCALL_SETUP();
    parser = (Parser) funcctx->user_fctx;
    values = palloc(parser->meta->tupdesc->natts * sizeof(Datum));
    isnull = palloc(parser->meta->tupdesc->natts * sizeof(bool));

    for(int i = 0; i < parser->meta->tupdesc->natts; i++)
    {
        values[i] = parser->g_values[i];
        isnull[i] = parser->g_isnull[i];
    }

    PG_TRY();
    {

        next = parser_next(parser, values, isnull);
    }
    PG_CATCH();
    {
        parser_close(parser);
        pfree(isnull);
        pfree(values);
        PG_RE_THROW();
    }
    PG_END_TRY();

    if(next)
    {
        HeapTuple tuple = heap_form_tuple(parser->meta->tupdesc, values, isnull);

        SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
    }

    parser_close(parser);
    pfree(isnull);
    pfree(values);
    SRF_RETURN_DONE(funcctx);
}
