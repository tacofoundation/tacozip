/*
 * tacozip.c — ZIP64 (STORE-only) writer with libzip backend and TACO Ghost supporting up to 7 metadata entries.
 *
 * This implementation replaces all custom ZIP code with libzip, while maintaining
 * the same API and ghost header concept. The ghost entry is now included in the
 * central directory as a normal file entry, but is physically first in the archive.
 *
 * Key changes from custom implementation:
 *  - Uses libzip for all ZIP operations (no more manual ZIP structures)
 *  - Always forces ZIP64 format regardless of file sizes
 *  - Always uses STORE method (no compression)
 *  - Ghost entry appears in central directory as normal entry
 *  - Maintains same API for backward compatibility
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

/* -------------------------- Little-endian readers -------------------------- */
static inline uint64_t le64_read(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i]) << (8u * i);
    return v;
}

/* ----------------------- Multi-parquet helper functions -------------------- */

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
 * @brief Convert taco_meta_array_t structure to arrays.
 * @param meta Input structure
 * @param offsets Output array of 7 offset values
 * @param lengths Output array of 7 length values
 */
static void meta_struct_to_arrays(const taco_meta_array_t *meta, uint64_t *offsets, uint64_t *lengths) {
    for (size_t i = 0; i < TACO_GHOST_MAX_ENTRIES; i++) {
        offsets[i] = meta->entries[i].offset;
        lengths[i] = meta->entries[i].length;
    }
}

/* ---------------------------- Ghost payload creator ------------------------ */
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

/* ---------------------------- Ghost payload parser ------------------------- */
/**
 * @brief Parse ghost payload into metadata structure.
 * @param payload Input buffer (must be at least TACO_GHOST_PAYLOAD_SIZE bytes)
 * @param meta Output metadata structure
 * @return TACOZ_OK on success, TACOZ_ERR_INVALID_GHOST on error
 */
static int parse_ghost_payload(const unsigned char *payload, taco_meta_array_t *meta) {
    memset(meta, 0, sizeof(*meta));
    
    /* Read count byte */
    meta->count = payload[0];
    if (meta->count > TACO_GHOST_MAX_ENTRIES) return TACOZ_ERR_INVALID_GHOST;

    /* Read all 7 pairs (even unused ones) */
    const unsigned char *pairs_start = payload + 4;
    for (size_t i = 0; i < TACO_GHOST_MAX_ENTRIES; i++) {
        meta->entries[i].offset = le64_read(pairs_start + i * 16 + 0);
        meta->entries[i].length = le64_read(pairs_start + i * 16 + 8);
    }

    return TACOZ_OK;
}

/* ----------------------- libzip helper functions --------------------------- */

/**
 * @brief Add file to libzip archive with STORE method and ZIP64.
 * @param za libzip archive handle
 * @param src_path Source file path
 * @param arc_name Archive entry name
 * @return TACOZ_OK on success, error code on failure
 */
static int add_file_to_archive(zip_t *za, const char *src_path, const char *arc_name) {
    /* Create source from file */
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

    /* Suppress unused parameter warnings for UTF-8 handling */
    (void)index;

    return TACOZ_OK;
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

    /* Suppress unused parameter warnings */
    (void)index;

    return TACOZ_OK;
}

/* ========================================================================== */
/*                            NEW MULTI-PARQUET API                          */
/* ========================================================================== */

int tacozip_create_multi(const char *zip_path,
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

int tacozip_read_ghost_multi(const char *zip_path, taco_meta_array_t *out) {
    if (!zip_path || !out) return TACOZ_ERR_PARAM;
    
    int error;
    zip_t *za = zip_open(zip_path, ZIP_RDONLY, &error);
    if (!za) {
        return TACOZ_ERR_IO;
    }

    /* Find ghost entry */
    zip_int64_t ghost_index = zip_name_locate(za, TACO_GHOST_NAME, 0);
    if (ghost_index < 0) {
        zip_close(za);
        return TACOZ_ERR_INVALID_GHOST;
    }

    /* Open ghost file */
    zip_file_t *ghost_file = zip_fopen_index(za, (zip_uint64_t)ghost_index, 0);
    if (!ghost_file) {
        zip_close(za);
        return TACOZ_ERR_LIBZIP;
    }

    /* Read ghost payload */
    unsigned char payload[TACO_GHOST_PAYLOAD_SIZE];
    zip_int64_t bytes_read = zip_fread(ghost_file, payload, sizeof(payload));
    zip_fclose(ghost_file);
    zip_close(za);

    if (bytes_read != sizeof(payload)) {
        return TACOZ_ERR_INVALID_GHOST;
    }

    /* Parse payload */
    return parse_ghost_payload(payload, out);
}

int tacozip_update_ghost_multi(const char *zip_path,
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

/* ========================================================================== */
/*                         LEGACY SINGLE-ENTRY API                           */
/* ========================================================================== */

int tacozip_create(const char *zip_path,
                   const char * const *src_files,
                   const char * const *arc_files,
                   size_t num_files,
                   uint64_t meta_offset,
                   uint64_t meta_length)
{
    if (!zip_path || !src_files || !arc_files || num_files == 0)
        return TACOZ_ERR_PARAM;

    /* Convert single entry to arrays */
    uint64_t offsets[TACO_GHOST_MAX_ENTRIES] = {0};
    uint64_t lengths[TACO_GHOST_MAX_ENTRIES] = {0};
    
    offsets[0] = meta_offset;
    lengths[0] = meta_length;

    return tacozip_create_multi(zip_path, src_files, arc_files, num_files,
                               offsets, lengths, TACO_GHOST_MAX_ENTRIES);
}

int tacozip_read_ghost(const char *zip_path, taco_meta_ptr_t *out) {
    if (!zip_path || !out) return TACOZ_ERR_PARAM;
    
    /* Use multi-reader and extract first entry */
    taco_meta_array_t multi = {0};
    int rc = tacozip_read_ghost_multi(zip_path, &multi);
    if (rc != TACOZ_OK) return rc;
    
    /* Return first entry (or 0,0 if no entries) */
    if (multi.count > 0) {
        out->offset = multi.entries[0].offset;
        out->length = multi.entries[0].length;
    } else {
        out->offset = 0;
        out->length = 0;
    }
    
    return TACOZ_OK;
}

int tacozip_update_ghost(const char *zip_path, uint64_t new_offset, uint64_t new_length) {
    if (!zip_path) return TACOZ_ERR_PARAM;
    
    /* Read current ghost state */
    taco_meta_array_t meta = {0};
    int rc = tacozip_read_ghost_multi(zip_path, &meta);
    if (rc != TACOZ_OK) return rc;
    
    /* Update first entry, preserve others */
    meta.entries[0].offset = new_offset;
    meta.entries[0].length = new_length;
    
    /* Recalculate count (in case first entry became 0,0) */
    uint64_t offsets[TACO_GHOST_MAX_ENTRIES];
    uint64_t lengths[TACO_GHOST_MAX_ENTRIES];
    meta_struct_to_arrays(&meta, offsets, lengths);
    meta.count = count_valid_entries(offsets, lengths);
    
    /* Use multi-updater */
    return tacozip_update_ghost_multi(zip_path, offsets, lengths, TACO_GHOST_MAX_ENTRIES);
}