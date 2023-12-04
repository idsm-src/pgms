/*
 * This file is part of the PGMS PostgreSQL extension distribution
 * available at https://bioinfo.uochb.cas.cz/gitlab/chemdb/pgms.
 *
 * Copyright (c) 2022 Jakub Galgonek
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

#ifndef INPUT_H_
#define INPUT_H_

#include <postgres.h>
#if PG_VERSION_NUM >= 160000
#include <varatt.h>
#endif
#include <libpq/libpq-fs.h>
#include <storage/large_object.h>


#define BUFFER_SIZE     (1 << 20) //1MB


typedef struct
{
    LargeObjectDesc *file;
    int pos;
    int size;
    char *data;
}
Input;


inline static Input *input_open_lo(Oid oid)
{
    Input *input = palloc(sizeof(Input));

    input->pos = 0;
    input->file = inv_open(oid, INV_READ, CurrentMemoryContext);
    input->size = 0;
    input->data = palloc(BUFFER_SIZE);

    return input;
}


inline static Input *input_open_varchar(VarChar *in)
{
    Input *input = palloc(sizeof(Input));

    input->pos = 0;
    input->file = NULL;
    input->size = VARSIZE(in) - VARHDRSZ;
    input->data = palloc(input->size);
    memcpy(input->data, VARDATA(in), input->size);

    return input;
}


inline static void input_close(Input *input)
{
    if(input->file)
    {
        close_lo_relation(true);
        inv_close(input->file);
    }

    pfree(input->data);
    pfree(input);
}


inline static int input_getc(Input *input)
{
    if(input->pos == input->size && input->file != NULL)
    {
        input->pos = 0;
        input->size = inv_read(input->file, input->data, BUFFER_SIZE);
    }

    if(input->pos == input->size)
        return EOF;

    return input->data[input->pos++];
}


inline static int input_ungetc(Input *input, int c)
{
    if(input->pos == 0)
        return EOF;

    input->data[--input->pos] = c;

    return c;
}


inline static bool input_eof(Input *input)
{
    if(input->pos == input->size && input->file != NULL)
    {
        input->pos = 0;
        input->size = inv_read(input->file, input->data, BUFFER_SIZE);
    }

    return input->pos == input->size;
}


static int input_read_line(Input *input, StringInfo buffer)
{
    resetStringInfo(buffer);

    do
    {
        int c = input_getc(input);

        if(c == '\0')
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Unsupported character: '\\0'")));

        if(c == '\n' && buffer->len > 0 && buffer->data[buffer->len - 1] == '\r')
            buffer->data[--buffer->len] = '\0';

        if(c == '\n' || c == EOF)
            break;

        appendStringInfoCharMacro(buffer, c);
    }
    while(true);

    return buffer->len;
}

#endif /* INPUT_H_ */
