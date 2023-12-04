pglibdir = $(POSTGRESQL_LIBDIR)
pglib_LTLIBRARIES = libpgms.la


libpgms_la_SOURCES = \
		enum.h \
		pgms.c \
		pgms.h \
		spectrum.c \
		spectrum.h \
		import/input.h \
		import/mgf.c \
		import/sdf.c \
		import/return.h \
		similarity/cosine_greedy.c \
		similarity/cosine_hungarian.c \
		similarity/cosine.h \
		similarity/intersect_mz_match.c \
		similarity/lsap.c \
		similarity/lsap.h \
		similarity/modified_cosine.c \
		similarity/precurzor_mz_match.c


libpgms_la_LDFLAGS = -lm
libpgms_la_CPPFLAGS = -std=gnu99 -O3 -fno-math-errno $(POSTGRESQL_CPPFLAGS)
