set(SRCS "lib/lz4.c"
        "lib/lz4frame.c"
        "lib/lz4hc.c"
        "lib/xxhash.c"
)

set(INCLUDE_DIRS "lib")

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES driver
)
