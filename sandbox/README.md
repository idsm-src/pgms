# A sandbox folder to evaluate pgms capacities


## Large experimental spectral lib

Downloading GNPS experimental libs

`wget -O gnps.mgf https://gnps-external.ucsd.edu/gnpslibrary/ALL_GNPS.mgf`


```
create table spectrums_gnps (
  scans integer,
  ionmode varchar,
  charge integer,
  name varchar,
  pepmass float,
  libraryquality integer,
  smiles varchar,
  inchi varchar,
  inchiaux varchar,
  datacollector varchar,
  filename varchar,
  mslevel varchar,
  organism varchar,
  pi varchar,
  pubmed varchar,
  seq varchar,
  source_instrument varchar,
  spectrumid varchar,
  submituser varchar,
  spectrum pgms.spectrum
);


test=> \lo_import gnps.mgf

insert into spectrums_gnps select * from pgms.load_from_mgf(:LASTOID) as (
    "SCANS" integer,
    "IONMODE" varchar,
    "CHARGE" integer,
    "NAME" varchar,
    "PEPMASS" float,
    "LIBRARYQUALITY" integer,
    "SMILES" varchar,
    "INCHI" varchar,
    "INCHIAUX" varchar,
    "DATACOLLECTOR" varchar,
    "FILENAME" varchar,
    "MSLEVEL" varchar,
    "ORGANISM" varchar,
    "PI" varchar,
    "PUBMED" varchar,
    "SEQ" varchar,
    "SOURCE_INSTRUMENT" varchar,
    "SPECTRUMID" varchar,
    "SUBMITUSER" varchar,
    spectrum pgms.spectrum
);
```

Returns a 
ERROR:  row is too big: size 12328, maximum size 8160

(working on a subset of the gnps_library)


A query matching compounds from one table (spectrums_gnps)
to another (spectrums)
```
select found_molecular_formula, found_inchi, found_pepmass, found_smiles, searched_smiles, cosine_greedy_score from
(
  select 
    s.molecular_formula as found_molecular_formula, 
    s.inchi as found_inchi,
    s.pepmass as found_pepmass, 
    s.smiles as found_smiles, 
    q.smiles as searched_smiles, 
    pgms.cosine_greedy(s.spectrum, q.spectrum) as cosine_greedy_score 
      from spectrums as s,
  (
    select spectrum, smiles from spectrums_gnps where name = 'Glucopiericidin M+H'
  ) as q
) as t
where cosine_greedy_score > 0.2
order by cosine_greedy_score desc;
```

Testing to filter by parent mass difference (not working)

```
select found_molecular_formula, found_inchi, found_pepmass, found_smiles, searched_smiles, cosine_greedy_score from
(
  select 
    s.molecular_formula as found_molecular_formula, 
    s.inchi as found_inchi,
    s.pepmass as found_pepmass, 
    s.smiles as found_smiles, 
    q.smiles as searched_smiles, 
    pgms.cosine_greedy(s.spectrum, q.spectrum) as cosine_greedy_score 
      from spectrums as s,
    s.pepmass - q.pepmass as mass_diff,
  (
    select spectrum, smiles, pepmass from spectrums_gnps where name = 'Glucopiericidin M+H'
  ) as q
) as t
where cosine_greedy_score > 0.2
order by cosine_greedy_score desc;
```

Testing batch mode. Working !!!

```
select found_molecular_formula, found_inchi, found_pepmass, found_smiles, searched_smiles, cosine_greedy_score from
(
  select 
    s.molecular_formula as found_molecular_formula, 
    s.inchi as found_inchi,
    s.pepmass as found_pepmass, 
    s.smiles as found_smiles, 
    q.smiles as searched_smiles, 
    pgms.cosine_greedy(s.spectrum, q.spectrum) as cosine_greedy_score 
      from spectrums as s,
  (
    select spectrum, smiles from spectrums_gnps
  ) as q
) as t
where cosine_greedy_score > 0.8
order by cosine_greedy_score desc;
```

Saving results of query to csv 

## exporting as csv

`\copy spectrums_gnps to ./test_copy.csv CSV HEADER`


To save the output of a query 

run 

`\o filename.txt`

then run your query

As per https://stackoverflow.com/a/55082741/4908629


### testing new pgms version 

The previous error " Returns a 
ERROR:  row is too big: size 12328, maximum size 8160 "
 is not appearing anymore when loading gnps spectral lib.


Testing batch match again.

Make a sublib 

`head -n 30000 gnps.mgf > gnps_30000.mgf``

Make sure to remove last lines to have a correct mgf file

import sub lib 

```
create table query (
  scans integer,
  ionmode varchar,
  charge integer,
  name varchar,
  pepmass float,
  libraryquality integer,
  smiles varchar,
  inchi varchar,
  inchiaux varchar,
  datacollector varchar,
  filename varchar,
  mslevel varchar,
  organism varchar,
  pi varchar,
  pubmed varchar,
  seq varchar,
  source_instrument varchar,
  spectrumid varchar,
  submituser varchar,
  spectrum pgms.spectrum
);


test=> \lo_import gnps_30000.mgf

insert into query select * from pgms.load_from_mgf(:LASTOID) as (
    "SCANS" integer,
    "IONMODE" varchar,
    "CHARGE" integer,
    "NAME" varchar,
    "PEPMASS" float,
    "LIBRARYQUALITY" integer,
    "SMILES" varchar,
    "INCHI" varchar,
    "INCHIAUX" varchar,
    "DATACOLLECTOR" varchar,
    "FILENAME" varchar,
    "MSLEVEL" varchar,
    "ORGANISM" varchar,
    "PI" varchar,
    "PUBMED" varchar,
    "SEQ" varchar,
    "SOURCE_INSTRUMENT" varchar,
    "SPECTRUMID" varchar,
    "SUBMITUSER" varchar,
    spectrum pgms.spectrum
);
```
Batch mode search 

```
select found_molecular_formula, found_inchi, found_pepmass, found_smiles, found_name, searched_smiles, searched_name, cosine_greedy_score from
(
  select 
    s.molecular_formula as found_molecular_formula, 
    s.inchi as found_inchi,
    s.pepmass as found_pepmass, 
    s.smiles as found_smiles, 
    s.name as found_name,
    q.smiles as searched_smiles, 
    q.name as searched_name,
    pgms.cosine_greedy(s.spectrum, q.spectrum) as cosine_greedy_score 
      from spectrums as s,
  (
    select spectrum, smiles, name from query
  ) as q
) as t
where cosine_greedy_score > 0.8
order by cosine_greedy_score desc;
```

This looks suuuuuper quick.
And there is NO parent mass filtering here ! Will need to benchmark.

Outputing the results to csv 


`\o filename.txt`

then run your query

File is here 

Testing direct output as csv (the previous line returns a text )

`\copy sandbox/gnps_qery.sql to ./gnps_query_results.csv CSV HEADER
`
`ERROR:  syntax error at or near "/"
LINE 1: COPY  sandbox/gnps_qery.sql TO STDOUT CSV HEADER
`

So when saving the query as .sql it can be called likes this

`\i sandbox/gnps_query.sql`

So I try

`\copy \i sandbox/gnps_query.sql to ./gnps_query_results.csv CSV HEADER
`

Still not good 
```test=> \copy \i sandbox/gnps_query.sql to ./gnps_query_results.csv CSV HEADER
\copy: parse error at "sandbox/gnps_query"```


This is working ?
```COPY \i sandbox/gnps_query.sql TO './gnps_query_results.csv'  WITH DELIMITER ',' CSV HEADER;```

Nope extra arg are ignored 

```

test=> COPY \i sandbox/gnps_query.sql TO './gnps_query_results.csv'  WITH DELIMITER ',' CSV HEADER;
\i: extra argument "TO" ignored
\i: extra argument "./gnps_query_results.csv" ignored
\i: extra argument "WITH" ignored
\i: extra argument "DELIMITER" ignored
\i: extra argument "," ignored
\i: extra argument "CSV" ignored
\i: extra argument "HEADER;" ignored

```

```COPY 'sandbox/gnps_query.sql' TO './gnps_query_results.csv'  WITH DELIMITER ',' CSV HEADER;```

Apparently it's not that simple ....

found this https://stackoverflow.com/a/42065966

\copy ($(<sandbox/gnps_query.sql)) to './gnps_query_results.csv' CSV HEADER

Not working ... 
test=> \copy ($(<sandbox/gnps_query.sql)) to './gnps_query_results.csv' CSV HEADER
ERROR:  syntax error at or near "$"
LINE 1: COPY  ( $ ( <sandbox/gnps_query.sql ) ) TO STDOUT CSV HEADER

Testing this 

https://stackoverflow.com/questions/39046908/psql-read-sql-file-and-output-to-csv

Working


