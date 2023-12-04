-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgms" to load this file. \quit


CREATE FUNCTION spectrum_is_equal_to(spectrum,spectrum) RETURNS bool AS 'MODULE_PATHNAME' LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;
CREATE FUNCTION spectrum_is_not_equal_to(spectrum,spectrum) RETURNS bool AS 'MODULE_PATHNAME' LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;
CREATE FUNCTION spectrum_is_less_than(spectrum,spectrum) RETURNS bool AS 'MODULE_PATHNAME' LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;
CREATE FUNCTION spectrum_is_greater_than(spectrum,spectrum) RETURNS bool AS 'MODULE_PATHNAME' LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;
CREATE FUNCTION spectrum_is_not_less_than(spectrum,spectrum) RETURNS bool AS 'MODULE_PATHNAME' LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;
CREATE FUNCTION spectrum_is_not_greater_than(spectrum,spectrum) RETURNS bool AS 'MODULE_PATHNAME' LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;
CREATE FUNCTION spectrum_compare(spectrum,spectrum) RETURNS int4 AS 'MODULE_PATHNAME' LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;


CREATE OPERATOR = (
    leftarg = spectrum,
    rightarg = spectrum,
    procedure = spectrum_is_equal_to,
    commutator = =,
    negator = !=,
    hashes, merges
);

CREATE OPERATOR != (
    leftarg = spectrum,
    rightarg = spectrum,
    procedure = spectrum_is_not_equal_to,
    commutator = !=,
    negator = =,
    hashes, merges
);

CREATE OPERATOR < (
    leftarg = spectrum,
    rightarg = spectrum,
    procedure = spectrum_is_less_than,
    commutator = >,
    negator = >=,
    hashes, merges
);

CREATE OPERATOR > (
    leftarg = spectrum,
    rightarg = spectrum,
    procedure = spectrum_is_greater_than,
    commutator = <,
    negator = <=,
    hashes, merges
);

CREATE OPERATOR >= (
    leftarg = spectrum,
    rightarg = spectrum,
    procedure = spectrum_is_not_less_than,
    commutator = <=,
    negator = <,
    hashes, merges
);

CREATE OPERATOR <= (
    leftarg = spectrum,
    rightarg = spectrum,
    procedure = spectrum_is_not_greater_than,
    commutator = >=,
    negator = >,
    hashes, merges
);


CREATE OPERATOR CLASS spectrum DEFAULT FOR TYPE spectrum USING btree AS
    OPERATOR   1   <,
    OPERATOR   2   <=,
    OPERATOR   3   =,
    OPERATOR   4   >=,
    OPERATOR   5   >,
    FUNCTION   1   spectrum_compare;
