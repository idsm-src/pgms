pglibdir = $(POSTGRESQL_LIBDIR)
pglib_LTLIBRARIES = libpgms.la


libpgms_la_SOURCES = \
		cosine_greedy.c \
		cosine_hungarian.c \
		cosine.h \
		lsap.c \
		lsap.h \
		mgf.c \
		json.c \
		pgms.c \
		pgms.h \
		spectrum.c \
		intersect_mz_match.c \
		precurzor_mz_match.c \
		modified_cosine.c


libpgms_la_LDFLAGS = -lm
libpgms_la_CPPFLAGS = -std=gnu99 -O3 -fno-math-errno $(POSTGRESQL_CPPFLAGS)
