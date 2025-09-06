/*
 * tacozip.c — ZIP64 (STORE-only) writer with libzip backend and TACO Ghost supporting up to 7 metadata entries.
 *
 * This simplified implementation provides a clean API with only the essential functions:
 * - tacozip_create() - create archive with up to 7 metadata entries
 * - tacozip_update_ghost() - write/update ghost metadata (no reading)
 * - tacozip_append_file() - append single file to archive
 * - tacozip_replace_file() - replace existing file in archive
 * - tacozip_get_version() - get library version
 */

/* Platform-specific feature detection */
#if defined(__linux__) || defined(__gnu_linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#elif defined(__APPLE__) || defined(__MACH__)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#elif defined(_WIN32) || defined(_WIN64)
/* Windows-specific includes handled separately */
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64  /* large-file I/O on POSIX */
#endif

#include "tacozip.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#define fileno _fileno
#else
#include <unistd.h>
#endif

/* libzip includes */
#include <zip.h>

/* ------------------------------- Tunables ---------------------------------- */
/* These can be overridden at compile time (CMake passes -D… if desired). */
#ifndef TACOZ_COPY_BUFSZ
#define TACOZ_COPY_BUFSZ (1u << 20)    /* 1 MiB copy buffer */
#endif
#ifndef TACOZ_SET_UTF8_FLAG
#define TACOZ_SET_UTF8_FLAG 0          /* set GP bit 11 if caller guarantees UTF-8 names */
#endif

/* -------------------------- Little-endian writers -------------------------- */
static inline void le64(unsigned char *p, uint64_t v){
    p[0] = (unsigned char)(v      );
    p[1] = (unsigned char)(v >> 8 );
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
    p[4] = (unsigned char)(v >> 32);
    p[5] = (unsigned char)(v >> 40);
    p[6] = (unsigned char)(v >> 48);
    p[7] = (unsigned char)(v >> 56);
}

/* ----------------------- Metadata helper functions -------------------- */

/**
 * @brief Count valid metadata entries by scanning until first (0,0) pair.
 * @param offsets Array of 7 offset values
 * @param lengths Array of 7 length values  
 * @return Number of valid entries (0-7)
 */
static uint8_t count_valid_entries(const uint64_t *offsets, const uint64_t *lengths) {
    for (size_t i = 0; i < TACO_GHOST_MAX_ENTRIES; i++) {
        if (offsets[i] == 0 && lengths[i] == 0) {
            return (uint8_t)i;  /* Found first (0,0) pair */
        }
    }
    return TACO_GHOST_MAX_ENTRIES;  /* All 7 entries are valid */
}

/**
 * @brief Convert arrays to taco_meta_array_t structure.
 * @param offsets Input array of 7 offset values
 * @param lengths Input array of 7 length values
 * @param out Output structure
 */
static void arrays_to_meta_struct(const uint64_t *offsets, const uint64_t *lengths, taco_meta_array_t *out) {
    out->count = count_valid_entries(offsets, lengths);
    for (size_t i = 0; i < TACO_GHOST_MAX_ENTRIES; i++) {
        out->entries[i].offset = offsets[i];
        out->entries[i].length = lengths[i];
    }
}

/**
 * @brief Create ghost payload from metadata structure.
 * @param meta Input metadata structure
 * @param payload Output buffer (must be at least TACO_GHOST_PAYLOAD_SIZE bytes)
 */
static void create_ghost_payload(const taco_meta_array_t *meta, unsigned char *payload) {
    memset(payload, 0, TACO_GHOST_PAYLOAD_SIZE);
    
    /* Count byte + 3 padding bytes for alignment */
    payload[0] = meta->count;
    payload[1] = payload[2] = payload[3] = 0;  /* padding */

    /* 7 pairs of (offset, length) - 112 bytes total */
    unsigned char *pairs_start = payload + 4;
    for (size_t i = 0; i < TACO_GHOST_MAX_ENTRIES; i++) {
        le64(pairs_start + i * 16 + 0, meta->entries[i].offset);
        le64(pairs_start + i * 16 + 8, meta->entries[i].length);
    }
}

/* ----------------------- libzip helper functions --------------------------- */

/**
 * @brief Check if a path is a directory
 * @param path File system path to check  
 * @return 1 if directory, 0 if file, -1 if error/doesn't exist
 */
static int is_directory(const char *src_path) {
    struct stat st;
    if (stat(src_path, &st) != 0) {
        return -1;  /* Error or doesn't exist */
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

/**
 * @brief Add file OR directory to libzip archive with STORE method and ZIP64.
 * @param za libzip archive handle
 * @param src_path Source file or directory path
 * @param arc_name Archive entry name
 * @return TACOZ_OK on success, error code on failure
 */
static int add_file_to_archive(zip_t *za, const char *src_path, const char *arc_name) {
    int path_type = is_directory(src_path);
    
    if (path_type == 1) {
        /* It's a directory - create empty entry with '/' suffix */
        char *dir_name = NULL;
        size_t name_len = strlen(arc_name);
        
        if (name_len > 0 && arc_name[name_len - 1] == '/') {
            dir_name = strdup(arc_name);
        } else {
            dir_name = malloc(name_len + 2);
            if (!dir_name) return TACOZ_ERR_IO;
            strcpy(dir_name, arc_name);
            strcat(dir_name, "/");
        }
        
        /* Create empty source for directory */
        zip_source_t *source = zip_source_buffer(za, "", 0, 0);
        if (!source) {
            free(dir_name);
            return TACOZ_ERR_LIBZIP;
        }
        
        /* Add directory entry */
        zip_int64_t index = zip_file_add(za, dir_name, source, ZIP_FL_OVERWRITE);
        if (index < 0) {
            zip_source_free(source);
            free(dir_name);
            return TACOZ_ERR_LIBZIP;
        }
        
        /* Set directory attributes */
        zip_uint32_t external_attr = 0755 | S_IFDIR;  /* Directory permissions with S_IFDIR flag */
        external_attr = external_attr << 16;  /* Shift to high 16 bits for Unix attributes */
        
        if (zip_file_set_external_attributes(za, (zip_uint64_t)index, 
                                            ZIP_FL_UNCHANGED, ZIP_OPSYS_UNIX, external_attr) < 0) {
            free(dir_name);
            return TACOZ_ERR_LIBZIP;
        }
        
        /* Force STORE method (no compression) */
        if (zip_set_file_compression(za, (zip_uint64_t)index, ZIP_CM_STORE, 0) < 0) {
            free(dir_name);
            return TACOZ_ERR_LIBZIP;
        }
        
        free(dir_name);
        return TACOZ_OK;
        
    } else if (path_type == 0) {
        /* It's a file - use original logic */
        zip_source_t *source = zip_source_file(za, src_path, 0, -1);
        if (!source) {
            return TACOZ_ERR_IO;
        }

        /* Add file to archive */
        zip_int64_t index = zip_file_add(za, arc_name, source, ZIP_FL_OVERWRITE);
        if (index < 0) {
            zip_source_free(source);
            return TACOZ_ERR_LIBZIP;
        }

        /* Force STORE method (no compression) */
        if (zip_set_file_compression(za, (zip_uint64_t)index, ZIP_CM_STORE, 0) < 0) {
            return TACOZ_ERR_LIBZIP;
        }

        return TACOZ_OK;
        
    } else {
        /* Path doesn't exist or error */
        return TACOZ_ERR_IO;
    }
}

/**
 * @brief Add ghost entry to libzip archive.
 * @param za libzip archive handle
 * @param meta Metadata structure for ghost payload
 * @return TACOZ_OK on success, error code on failure
 */
static int add_ghost_to_archive(zip_t *za, const taco_meta_array_t *meta) {
    /* Create ghost payload */
    unsigned char *payload = malloc(TACO_GHOST_PAYLOAD_SIZE);
    if (!payload) return TACOZ_ERR_IO;
    
    create_ghost_payload(meta, payload);

    /* Create source from buffer */
    zip_source_t *source = zip_source_buffer(za, payload, TACO_GHOST_PAYLOAD_SIZE, 1); /* 1 = freep */
    if (!source) {
        free(payload);
        return TACOZ_ERR_LIBZIP;
    }

    /* Add ghost entry to archive */
    zip_int64_t index = zip_file_add(za, TACO_GHOST_NAME, source, ZIP_FL_OVERWRITE);
    if (index < 0) {
        zip_source_free(source);
        return TACOZ_ERR_LIBZIP;
    }

    /* Force STORE method (no compression) */
    if (zip_set_file_compression(za, (zip_uint64_t)index, ZIP_CM_STORE, 0) < 0) {
        return TACOZ_ERR_LIBZIP;
    }

    return TACOZ_OK;
}

/* ========================================================================== */
/*                               CORE API                                    */
/* ========================================================================== */

const char* tacozip_get_version(void) {
    return TACOZIP_VERSION_STRING;
}

int tacozip_create(const char *zip_path,
                  const char * const *src_files,
                  const char * const *arc_files,
                  size_t num_files,
                  const uint64_t *meta_offsets,
                  const uint64_t *meta_lengths,
                  size_t array_size)
{
    if (!zip_path || !src_files || !arc_files || num_files == 0)
        return TACOZ_ERR_PARAM;
    
    if (!meta_offsets || !meta_lengths || array_size != TACO_GHOST_MAX_ENTRIES)
        return TACOZ_ERR_PARAM;

    int error;
    zip_t *za = zip_open(zip_path, ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!za) {
        return TACOZ_ERR_IO;
    }

    /* Convert arrays to metadata structure */
    taco_meta_array_t meta = {0};
    arrays_to_meta_struct(meta_offsets, meta_lengths, &meta);

    /* Add ghost entry first (so it appears at the beginning physically) */
    int rc = add_ghost_to_archive(za, &meta);
    if (rc != TACOZ_OK) {
        zip_close(za);
        return rc;
    }

    /* Add each regular file */
    for (size_t i = 0; i < num_files; i++) {
        if (!src_files[i] || !arc_files[i]) {
            zip_close(za);
            return TACOZ_ERR_PARAM;
        }
        
        rc = add_file_to_archive(za, src_files[i], arc_files[i]);
        if (rc != TACOZ_OK) {
            zip_close(za);
            return rc;
        }
    }

    /* Close and finalize the archive */
    if (zip_close(za) < 0) {
        return TACOZ_ERR_IO;
    }

    return TACOZ_OK;
}

int tacozip_update_ghost(const char *zip_path,
                        const uint64_t *meta_offsets,
                        const uint64_t *meta_lengths,
                        size_t array_size) {
    if (!zip_path || !meta_offsets || !meta_lengths || array_size != TACO_GHOST_MAX_ENTRIES)
        return TACOZ_ERR_PARAM;

    int error;
    zip_t *za = zip_open(zip_path, 0, &error);  /* Open for modification */
    if (!za) {
        return TACOZ_ERR_IO;
    }

    /* Find ghost entry */
    zip_int64_t ghost_index = zip_name_locate(za, TACO_GHOST_NAME, 0);
    if (ghost_index < 0) {
        zip_close(za);
        return TACOZ_ERR_INVALID_GHOST;
    }

    /* Convert arrays to metadata structure */
    taco_meta_array_t meta = {0};
    arrays_to_meta_struct(meta_offsets, meta_lengths, &meta);

    /* Create new ghost payload */
    unsigned char *payload = malloc(TACO_GHOST_PAYLOAD_SIZE);
    if (!payload) {
        zip_close(za);
        return TACOZ_ERR_IO;
    }
    
    create_ghost_payload(&meta, payload);

    /* Create source from buffer for replacement */
    zip_source_t *source = zip_source_buffer(za, payload, TACO_GHOST_PAYLOAD_SIZE, 1); /* 1 = freep */
    if (!source) {
        free(payload);
        zip_close(za);
        return TACOZ_ERR_LIBZIP;
    }

    /* Replace ghost entry */
    if (zip_file_replace(za, (zip_uint64_t)ghost_index, source, 0) < 0) {
        zip_source_free(source);
        zip_close(za);
        return TACOZ_ERR_LIBZIP;
    }

    /* Ensure STORE method is maintained */
    if (zip_set_file_compression(za, (zip_uint64_t)ghost_index, ZIP_CM_STORE, 0) < 0) {
        zip_close(za);
        return TACOZ_ERR_LIBZIP;
    }

    /* Close and finalize the archive */
    if (zip_close(za) < 0) {
        return TACOZ_ERR_IO;
    }

    return TACOZ_OK;
}

int tacozip_append_file(const char *zip_path,
                       const char *src_path,
                       const char *arc_name) {
    if (!zip_path || !src_path || !arc_name) {
        return TACOZ_ERR_PARAM;
    }

    /* Verify the source file exists and is readable */
    FILE *test_file = fopen(src_path, "rb");
    if (!test_file) {
        return TACOZ_ERR_IO;
    }
    fclose(test_file);

    int error;
    zip_t *za = zip_open(zip_path, 0, &error);  /* Open for modification */
    if (!za) {
        return TACOZ_ERR_IO;
    }

    /* Check if file already exists */
    zip_int64_t existing_index = zip_name_locate(za, arc_name, 0);
    if (existing_index >= 0) {
        zip_close(za);
        return TACOZ_ERR_EXISTS;
    }

    /* Protect against adding duplicate ghost entry */
    if (strcmp(arc_name, TACO_GHOST_NAME) == 0) {
        zip_close(za);
        return TACOZ_ERR_PARAM;  /* Cannot add duplicate ghost */
    }

    /* Add the new file */
    int rc = add_file_to_archive(za, src_path, arc_name);
    if (rc != TACOZ_OK) {
        zip_close(za);
        return rc;
    }

    /* Close and finalize the archive */
    if (zip_close(za) < 0) {
        return TACOZ_ERR_IO;
    }

    return TACOZ_OK;
}

int tacozip_replace_file(const char *zip_path,
                        const char *file_name,
                        const char *new_src_path) {
    if (!zip_path || !file_name || !new_src_path) {
        return TACOZ_ERR_PARAM;
    }

    /* Verify the new source file exists and is readable */
    FILE *test_file = fopen(new_src_path, "rb");
    if (!test_file) {
        return TACOZ_ERR_IO;
    }
    fclose(test_file);

    int error;
    zip_t *za = zip_open(zip_path, 0, &error);  /* Open for modification */
    if (!za) {
        return TACOZ_ERR_IO;
    }

    /* Find the file to replace */
    zip_int64_t file_index = zip_name_locate(za, file_name, 0);
    if (file_index < 0) {
        zip_close(za);
        return TACOZ_ERR_NOT_FOUND;
    }

    /* Protect against replacing the ghost entry */
    if (strcmp(file_name, TACO_GHOST_NAME) == 0) {
        zip_close(za);
        return TACOZ_ERR_PARAM;  /* Cannot replace ghost with this function */
    }

    /* Create source from new file */
    zip_source_t *source = zip_source_file(za, new_src_path, 0, -1);
    if (!source) {
        zip_close(za);
        return TACOZ_ERR_IO;
    }

    /* Replace the file */
    if (zip_file_replace(za, (zip_uint64_t)file_index, source, 0) < 0) {
        zip_source_free(source);
        zip_close(za);
        return TACOZ_ERR_LIBZIP;
    }

    /* Ensure STORE method is maintained (no compression) */
    if (zip_set_file_compression(za, (zip_uint64_t)file_index, ZIP_CM_STORE, 0) < 0) {
        zip_close(za);
        return TACOZ_ERR_LIBZIP;
    }

    /* Close and finalize the archive */
    if (zip_close(za) < 0) {
        return TACOZ_ERR_IO;
    }

    return TACOZ_OK;
}