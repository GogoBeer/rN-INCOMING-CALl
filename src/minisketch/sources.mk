# - All variables are namespaced with MINISKETCH_ to avoid colliding with
#     downstream makefiles.
# - All Variables ending in _HEADERS or _SOURCES confuse automake, so the
#     _INT postfix is applied.
# - Convenience variables, for example a MINISKETCH_FIELDS_DIR should not be used
#     as they interfere with automatic dependency generation
# - The %reldir% is the relative path from the Makefile.am. This allows
#   downstreams to use these variables without having to manually account for
#   the path change.

MINISKETCH_INCLUDE_DIR_INT = %reldir%/include

MINISKETCH_DIST_HEADERS_INT =
MINISKETCH_DIST_HEADERS_INT += %reldir%/include/minisketch.h

MINISKETCH_LIB_HEADERS_INT =
MINISKETCH_LIB_HEADERS_INT += %reldir%/src/false_positives.h
MINISKETCH_LIB_HEADERS_INT += %reldir%/src/fielddefines.h
MINISKETCH_LIB_HEADERS_INT += %reldir%/src/int_utils.h
MINISKETCH_LIB_HEADERS_INT += %reldir%/src/lintrans.h
MINISKETCH_LIB_HEADERS_INT += %reldir%/src/sketch.h
MINISKETCH_LIB_HEADERS_INT += %reldir%/src/sketch_imp