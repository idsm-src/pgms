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

#ifndef RETURN_H_
#define RETURN_H_

#include <postgres.h>
#include <funcapi.h>
#include <catalog/pg_type.h>
#include <utils/lsyscache.h>
#include <utils/typcache.h>


typedef struct
{
    Oid typid;
    TupleDesc tupdesc;

    FmgrInfo *attinfuncs;
    Oid *attioparams;
    int32 *atttypmods;

    int32 *attbasetypids;
}
ReturnTypeMetadata;


static ReturnTypeMetadata *get_return_type_metadata(FunctionCallInfo fcinfo, const char *funcname, bool have_record_arg)
{
    TupleDesc tupdesc;
    Oid typeid = InvalidOid;

    if(!have_record_arg || PG_ARGISNULL(0) && get_fn_expr_argtype(fcinfo->flinfo, 0) == RECORDOID)
    {
        /* here it is syntactically impossible to specify the target type as domain-over-composite */

        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("could not determine row type for result of %s", funcname)));

        typeid = tupdesc->tdtypeid;
    }
    else
    {
        typeid = get_fn_expr_argtype(fcinfo->flinfo, 0);

        Oid base_typeid = typeid;
        int32 base_typmod = -1;

        if(typeid == RECORDOID)
        {
            HeapTupleHeader rec = PG_GETARG_HEAPTUPLEHEADER(0);
            base_typeid = HeapTupleHeaderGetTypeId(rec);
            base_typmod = HeapTupleHeaderGetTypMod(rec);
        }
        else
        {
            char type = get_typtype(typeid);

            if(type == TYPTYPE_DOMAIN)
            {
                base_typeid = getBaseTypeAndTypmod(typeid, &base_typmod);
                type = get_typtype(base_typeid);
            }

            if(type != TYPTYPE_COMPOSITE)
                ereport(ERROR, (errcode(ERRCODE_DATATYPE_MISMATCH), errmsg("first argument of %s must be a row type", funcname)));
        }

        TupleDesc tupdesc_base = lookup_rowtype_tupdesc(base_typeid, base_typmod);
        tupdesc = CreateTupleDescCopy(tupdesc_base);
        ReleaseTupleDesc(tupdesc_base);
    }


    ReturnTypeMetadata *meta = palloc(sizeof(ReturnTypeMetadata));

    meta->tupdesc = tupdesc;
    meta->typid = typeid;
    meta->attbasetypids = palloc0(tupdesc->natts * sizeof(Oid));
    meta->attinfuncs = palloc0(tupdesc->natts * sizeof(FmgrInfo));
    meta->attioparams = palloc0(tupdesc->natts * sizeof(Oid));
    meta->atttypmods = palloc0(tupdesc->natts * sizeof(int32));

    for(int i = 0; i < tupdesc->natts; i++)
    {
        Form_pg_attribute att = TupleDescAttr(tupdesc, i);
        meta->attbasetypids[i] = InvalidOid;

        if (!att->attisdropped)
        {
            Oid attinfuncid;
            getTypeInputInfo(att->atttypid, &attinfuncid, &meta->attioparams[i]);
            fmgr_info(attinfuncid, &meta->attinfuncs[i]);
            meta->atttypmods[i] = att->atttypmod;

            if(get_typtype(att->atttypid) == TYPTYPE_DOMAIN)
            {
                int32 typmod = att->atttypmod;
                meta->attbasetypids[i] = getBaseTypeAndTypmod(att->atttypid, &typmod);
            }
            else
            {
                meta->attbasetypids[i] = att->atttypid;
            }
        }
    }

    return meta;
}


static int find_attribute(ReturnTypeMetadata *meta, const char *name, int length)
{
    if(name == NULL || length == 0)
        return -1;

    for(int i = 0; i < meta->tupdesc->natts; i++)
    {
        if(TupleDescAttr(meta->tupdesc, i)->attisdropped)
            continue;

        char *field = NameStr(TupleDescAttr(meta->tupdesc, i)->attname);

        if(pg_strncasecmp(field, name, length))
            continue;

        if(field[length] == '\0')
            return i;
    }

    return -1;
}

#endif /* RETURN_H_ */
