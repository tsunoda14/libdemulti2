lib_LTLIBRARIES = libdemulti2.la

include_HEADERS = demulti2.h

libdemulti2_la_SOURCES = \
    demulti2.c \
    demulti2.h

if USE_PCSC
libdemulti2_la_SOURCES += \
    bcas_cardio.c \
    bcas_cardio.h

libdemulti2_la_CPPFLAGS = $(PCSCCPPFLAGS)
endif

if HOST_X86_64
libdemulti2_la_SOURCES += desc_avx.s
endif

libdemulti2_la_LDFLAGS = \
    -export-symbols-regex ^demulti2_ -version-info 1:0:1 -ldl

check_PROGRAMS = test1
test1_LDADD = -ldemulti2 -ldl
