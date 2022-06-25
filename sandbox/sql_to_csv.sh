#!/bin/bash
# sql_to_csv.sh

CONN="psql -U test -d test"
QUERY="$(sed 's/;//g;/^--/ d;s/--.*//g;' $1 | tr '\n' ' ')"
echo "$QUERY"

echo "\\copy ($QUERY) to '$2' CSV  HEADER DELIMITER E'\t'" | $CONN > /dev/null
