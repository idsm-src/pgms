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

#ifndef ENUM_H_
#define ENUM_H_

#include <postgres.h>
#include <catalog/pg_enum.h>
#include <utils/syscache.h>
#include <access/htup_details.h>


static inline Oid LookupExplicitEnumType(const Oid spaceid, const char *name)
{
    Oid typoid = GetSysCacheOid2(TYPENAMENSP, Anum_pg_type_oid, PointerGetDatum(name), ObjectIdGetDatum(spaceid));

    if(!OidIsValid(typoid))
        elog(ERROR, "cannot find enum type '%s'", name);

    return typoid;
}


static inline Oid LookupExplicitEnumValue(const Oid typoid, const char *name)
{
    HeapTuple tup = SearchSysCache2(ENUMTYPOIDNAME, ObjectIdGetDatum(typoid), CStringGetDatum(name));

    if(!HeapTupleIsValid(tup))
        elog(ERROR, "cannot find enum value '%s'", name);

    Oid valueid = ((Form_pg_enum) GETSTRUCT(tup))->oid;

    ReleaseSysCache(tup);

    return valueid;
}

#endif /* ENUM_H_ */
