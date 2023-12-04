-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgms" to load this file. \quit


CREATE TYPE spectrum;

CREATE FUNCTION spectrum_input(cstring) RETURNS spectrum  AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE STRICT;
CREATE FUNCTION spectrum_output(spectrum) RETURNS cstring AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE STRICT;

CREATE TYPE spectrum
(
    internallength = VARIABLE,
    input = spectrum_input,
    output = spectrum_output,
    alignment = float,
    storage = extended
);

CREATE TYPE tolerance AS ENUM ('DALTON', 'PPM');

CREATE FUNCTION spectrum_normalize(spectrum) RETURNS spectrum   AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE STRICT;
CREATE FUNCTION spectrum_max_intensity(spectrum) RETURNS float4 AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE STRICT;

CREATE FUNCTION cosine_greedy(spectrum, spectrum, float4 = 0.1) RETURNS float4 AS 'MODULE_PATHNAME','cosine_greedy_simple' LANGUAGE C STABLE PARALLEL SAFE STRICT COST 100;
CREATE FUNCTION cosine_greedy(spectrum, spectrum, float4, float4, float4) RETURNS float4 AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE STRICT COST 1000;
CREATE FUNCTION cosine_hungarian(spectrum, spectrum, float4 = 0.1, float4=0.0, float4=1.0) RETURNS float4 AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE STRICT COST 1000;
CREATE FUNCTION cosine_modified(spectrum, spectrum, float4, float4=0.1, float4=0.0, float4=1.0) RETURNS float4 AS 'MODULE_PATHNAME', 'modified_cosine' LANGUAGE C STABLE PARALLEL SAFE STRICT COST 1000;
CREATE FUNCTION intersect_mz(spectrum, spectrum, float4=0.1) RETURNS float4 AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE STRICT COST 1000;
CREATE FUNCTION precurzor_mz_match(float4, float4, float4=1.0, tolerance='DALTON') RETURNS float4 AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE STRICT COST 1000;

CREATE FUNCTION sdf_to_record(varchar, varchar='molfile') RETURNS record AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE;
CREATE FUNCTION sdf_to_recordset(varchar, varchar='molfile') RETURNS SETOF record AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE;
CREATE FUNCTION sdf_to_recordset(Oid, varchar='molfile') RETURNS SETOF record AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE;
CREATE FUNCTION sdf_populate_record(anynonarray, varchar, varchar='molfile') RETURNS anynonarray AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE;
CREATE FUNCTION sdf_populate_recordset(anynonarray, varchar, varchar='molfile') RETURNS SETOF anynonarray AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE;
CREATE FUNCTION sdf_populate_recordset(anynonarray, Oid, varchar='molfile') RETURNS SETOF anynonarray AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE;

CREATE FUNCTION mgf_to_record(varchar, varchar='pepintensity') RETURNS record AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE;
CREATE FUNCTION mgf_to_recordset(varchar, varchar='pepintensity') RETURNS SETOF record AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE;
CREATE FUNCTION mgf_to_recordset(Oid, varchar='pepintensity') RETURNS SETOF record AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE;
CREATE FUNCTION mgf_populate_record(anynonarray, varchar, varchar='pepintensity') RETURNS anynonarray            AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE;
CREATE FUNCTION mgf_populate_recordset(anynonarray, varchar, varchar='pepintensity') RETURNS SETOF anynonarray AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE;
CREATE FUNCTION mgf_populate_recordset(anynonarray, Oid, varchar='pepintensity') RETURNS SETOF anynonarray AS 'MODULE_PATHNAME' LANGUAGE C STABLE PARALLEL SAFE;
