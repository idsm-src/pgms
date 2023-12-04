/*
 * This file is part of the PGMS PostgreSQL extension distribution
 * available at https://bioinfo.uochb.cas.cz/gitlab/chemdb/pgms.
 *
 * Copyright (c) 2021-2022 Jakub Galgonek
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
#include <funcapi.h>
#include <catalog/namespace.h>
#include <utils/syscache.h>

PG_MODULE_MAGIC;


Oid spectrumOid;


void _PG_init()
{
    Oid spaceid = LookupExplicitNamespace("pgms", false);
    spectrumOid = GetSysCacheOid2(TYPENAMENSP, Anum_pg_type_oid, PointerGetDatum("spectrum"), ObjectIdGetDatum(spaceid));
}
