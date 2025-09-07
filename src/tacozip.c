/*
 * tacozip.c — ZIP64 (STORE-only) writer with libzip backend and TACO Ghost supporting up to 7 metadata entries.
 *
 * This simplified implementation provides a clean API with only the essential functions:
 * - tacozip_create() - create archive with up to 7 metadata entries
 * - tacozip_update_ghost() - write/update ghost metadata (optimized, bypasses libzip)
 * - tacozip_append_files() - append files to archive (optimized, bypasses libzip)
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

/* zlib for CRC32 */
#include <zlib.h>

/* ------------------------------- Tunables ---------------------------------- */
/* These can be overridden at compile time (CMake passes -D… if desired). */
#ifndef TACOZ_COPY_BUFSZ
#define TACOZ_COPY_BUFSZ (1u << 20)    /* 1 MiB copy buffer */
#endif
#ifndef TACOZ_SET_UTF8_FLAG
#define TACOZ_SET_UTF8_FLAG 0          /* set GP bit 11 if caller guarantees UTF-8 names */
#endif

/* Constants for better code readability */
#define ZIP_LFH_SIGNATURE 0x04034b50
#define ZIP_CDH_SIGNATURE 0x02014b50
#define ZIP_EOCD_SIGNATURE 0x06054b50
#define ZIP64_MARKER 0xFFFFFFFF
#define ZIP_VERSION_NEEDED_ZIP64 45

/* EOCD and search constants */
#define EOCD_MIN_SIZE 22
#define LARGE_FILE_THRESHOLD 1000000
#define LARGE_SEARCH_BUFFER 65536
#define SMALL_SEARCH_BUFFER 1024

/* -------------------------- Little-endian writers -------------------------- */
static inline void le16(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
}

static inline void le32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

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

/* ----------------------- Metadata helper functions ------------------------- */

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

/* ----------------------- Direct ZIP manipulation functions ----------------- */

/**
 * @brief Read existing Central Directory as binary blob
 * @param fp File pointer
 * @param cd_offset Output: offset where CD starts
 * @param cd_data Output: binary CD data (caller must free)
 * @param cd_size Output: size of CD data
 * @param total_entries Output: number of existing entries
 * @return TACOZ_OK on success, error code on failure
 */
static int read_existing_cd_blob(FILE *fp, uint64_t *cd_offset, unsigned char **cd_data, 
                                uint32_t *cd_size, uint16_t *total_entries) {
    /* Get file size using fseeko for large files */
    if (fseeko(fp, 0, SEEK_END) != 0) return TACOZ_ERR_IO;
    off_t file_size = ftello(fp);
    if (file_size < EOCD_MIN_SIZE) return TACOZ_ERR_INVALID_GHOST;
    
    /* Use larger search buffer for large files */
    size_t search_buffer_size = (file_size > LARGE_FILE_THRESHOLD) ? LARGE_SEARCH_BUFFER : SMALL_SEARCH_BUFFER;
    unsigned char *buffer = malloc(search_buffer_size);
    if (!buffer) return TACOZ_ERR_IO;
    
    /* Search for EOCD signature from end */
    off_t search_start = file_size - search_buffer_size;
    if (search_start < 0) search_start = 0;
    
    size_t bytes_to_read = file_size - search_start;
    if (fseeko(fp, search_start, SEEK_SET) != 0) {
        free(buffer);
        return TACOZ_ERR_IO;
    }
    
    size_t read_size = fread(buffer, 1, bytes_to_read, fp);
    if (read_size == 0) {
        free(buffer);
        return TACOZ_ERR_IO;
    }
    
    /* Look for EOCD signature */
    for (long i = read_size - EOCD_MIN_SIZE; i >= 0; i--) {
        if (buffer[i] == 0x50 && buffer[i+1] == 0x4b && 
            buffer[i+2] == 0x05 && buffer[i+3] == 0x06) {
            
            /* Found EOCD, extract info */
            unsigned char *eocd = buffer + i;
            *total_entries = eocd[10] | (eocd[11] << 8);
            *cd_size = eocd[12] | (eocd[13] << 8) | (eocd[14] << 16) | (eocd[15] << 24);
            
            /* Handle ZIP64 case */
            uint32_t cd_offset_32 = eocd[16] | (eocd[17] << 8) | (eocd[18] << 16) | (eocd[19] << 24);
            if (cd_offset_32 == ZIP64_MARKER) {
                /* This is ZIP64, we need to find ZIP64 EOCD record */
                /* For now, try to work with large offset as-is */
                *cd_offset = (uint64_t)cd_offset_32;
            } else {
                *cd_offset = (uint64_t)cd_offset_32;
            }
            
            /* Allocate buffer for CD data */
            *cd_data = malloc(*cd_size);
            if (!*cd_data) {
                free(buffer);
                return TACOZ_ERR_IO;
            }
            
            /* Read existing CD as binary blob using fseeko */
            if (fseeko(fp, *cd_offset, SEEK_SET) != 0) {
                free(*cd_data);
                free(buffer);
                return TACOZ_ERR_IO;
            }
            
            size_t bytes_read = fread(*cd_data, 1, *cd_size, fp);
            if (bytes_read != *cd_size) {
                free(*cd_data);
                free(buffer);
                return TACOZ_ERR_IO;
            }
            
            free(buffer);
            return TACOZ_OK;
        }
    }
    
    free(buffer);
    return TACOZ_ERR_INVALID_GHOST;
}

/**
 * @brief Check if filename already exists in existing CD blob
 * @param cd_data Binary CD data
 * @param cd_size Size of CD data
 * @param filename Filename to check
 * @return 1 if exists, 0 if not exists, -1 on error
 */
static int filename_exists_in_cd(const unsigned char *cd_data, uint32_t cd_size, const char *filename) {
    uint32_t offset = 0;
    
    while (offset < cd_size) {
        if (offset + 46 > cd_size) break;  /* Not enough space for CD header */
        
        /* Check CD signature */
        if (cd_data[offset] != 0x50 || cd_data[offset+1] != 0x4b ||
            cd_data[offset+2] != 0x01 || cd_data[offset+3] != 0x02) {
            break;  /* Invalid CD entry */
        }
        
        /* Extract lengths */
        uint16_t filename_len = cd_data[offset+28] | (cd_data[offset+29] << 8);
        uint16_t extra_len = cd_data[offset+30] | (cd_data[offset+31] << 8);
        uint16_t comment_len = cd_data[offset+32] | (cd_data[offset+33] << 8);
        
        /* Check if we have enough space */
        if (offset + 46 + filename_len > cd_size) break;
        
        /* Compare filename */
        if (filename_len == strlen(filename) && 
            memcmp(cd_data + offset + 46, filename, filename_len) == 0) {
            return 1;  /* Found */
        }
        
        /* Move to next entry */
        offset += 46 + filename_len + extra_len + comment_len;
    }
    
    return 0;  /* Not found */
}

/**
 * @brief Write a local file header for STORE method
 * @param fp File pointer positioned where to write LFH
 * @param filename Archive filename
 * @param file_size Uncompressed file size
 * @param crc32 CRC32 checksum
 * @return TACOZ_OK on success, error code on failure
 */
static int write_local_file_header(FILE *fp, const char *filename, uint64_t file_size, uint32_t crc32) {
    unsigned char header[30];
    uint16_t filename_len = (uint16_t)strlen(filename);
    
    /* Local file header signature */
    le32(header + 0, ZIP_LFH_SIGNATURE);
    
    /* Version needed to extract */
    le16(header + 4, ZIP_VERSION_NEEDED_ZIP64);
    
    /* General purpose bit flag */
    le16(header + 6, TACOZ_SET_UTF8_FLAG ? (1 << 11) : 0);
    
    /* Compression method (0 = STORE) */
    le16(header + 8, 0);
    
    /* File last modification time & date (dummy values) */
    le16(header + 10, 0);  /* time */
    le16(header + 12, 0);  /* date */
    
    /* CRC32 */
    le32(header + 14, crc32);
    
    /* Compressed size (same as uncompressed for STORE) */
    if (file_size >= ZIP64_MARKER) {
        le32(header + 18, ZIP64_MARKER);  /* ZIP64 marker */
        le32(header + 22, ZIP64_MARKER);  /* ZIP64 marker */
    } else {
        le32(header + 18, (uint32_t)file_size);
        le32(header + 22, (uint32_t)file_size);
    }
    
    /* Filename length */
    le16(header + 26, filename_len);
    
    /* Extra field length (0 for now, ZIP64 handling omitted for simplicity) */
    le16(header + 28, 0);
    
    /* Write header */
    if (fwrite(header, 1, 30, fp) != 30) return TACOZ_ERR_IO;
    
    /* Write filename */
    if (fwrite(filename, 1, filename_len, fp) != filename_len) return TACOZ_ERR_IO;
    
    return TACOZ_OK;
}

/**
 * @brief Copy file content while calculating CRC32
 * @param src_fp Source file pointer  
 * @param dest_fp Destination file pointer
 * @param crc32_out Output CRC32 value
 * @param size_out Output file size
 * @return TACOZ_OK on success, error code on failure
 */
static int copy_file_with_crc(FILE *src_fp, FILE *dest_fp, uint32_t *crc32_out, uint64_t *size_out) {
    unsigned char buffer[TACOZ_COPY_BUFSZ];
    uLong crc = crc32(0L, Z_NULL, 0);  /* Initialize CRC */
    uint64_t total_size = 0;
    
    while (!feof(src_fp)) {
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), src_fp);
        if (bytes_read == 0) break;
        
        /* Update CRC32 */
        crc = crc32(crc, buffer, bytes_read);
        
        /* Write to destination */
        if (fwrite(buffer, 1, bytes_read, dest_fp) != bytes_read) {
            return TACOZ_ERR_IO;
        }
        
        total_size += bytes_read;
    }
    
    *crc32_out = (uint32_t)crc;
    *size_out = total_size;
    return TACOZ_OK;
}

/**
 * @brief Write a Central Directory entry
 * @param fp File pointer
 * @param filename Archive filename  
 * @param local_offset Offset of local file header
 * @param file_size File size
 * @param crc32 CRC32 checksum
 * @return TACOZ_OK on success, error code on failure
 */
static int write_cd_entry(FILE *fp, const char *filename, uint64_t local_offset, uint64_t file_size, uint32_t crc32) {
    unsigned char header[46];
    uint16_t filename_len = (uint16_t)strlen(filename);
    
    /* Central directory file header signature */
    le32(header + 0, ZIP_CDH_SIGNATURE);
    
    /* Version made by */
    le16(header + 4, ZIP_VERSION_NEEDED_ZIP64);
    
    /* Version needed to extract */
    le16(header + 6, ZIP_VERSION_NEEDED_ZIP64);
    
    /* General purpose bit flag */
    le16(header + 8, TACOZ_SET_UTF8_FLAG ? (1 << 11) : 0);
    
    /* Compression method (0 = STORE) */
    le16(header + 10, 0);
    
    /* File last modification time & date (dummy values) */
    le16(header + 12, 0);  /* time */
    le16(header + 14, 0);  /* date */
    
    /* CRC32 */
    le32(header + 16, crc32);
    
    /* Compressed/uncompressed size (same for STORE) */
    if (file_size >= ZIP64_MARKER) {
        le32(header + 20, ZIP64_MARKER);  /* ZIP64 marker */
        le32(header + 24, ZIP64_MARKER);  /* ZIP64 marker */
    } else {
        le32(header + 20, (uint32_t)file_size);
        le32(header + 24, (uint32_t)file_size);
    }
    
    /* Filename length */
    le16(header + 28, filename_len);
    
    /* Extra field length */
    le16(header + 30, 0);
    
    /* Comment length */
    le16(header + 32, 0);
    
    /* Disk number start */
    le16(header + 34, 0);
    
    /* Internal file attributes */
    le16(header + 36, 0);
    
    /* External file attributes */
    le32(header + 38, 0);
    
    /* Local header offset */
    if (local_offset >= ZIP64_MARKER) {
        le32(header + 42, ZIP64_MARKER);  /* ZIP64 marker */
    } else {
        le32(header + 42, (uint32_t)local_offset);
    }
    
    /* Write header */
    if (fwrite(header, 1, 46, fp) != 46) return TACOZ_ERR_IO;
    
    /* Write filename */
    if (fwrite(filename, 1, filename_len, fp) != filename_len) return TACOZ_ERR_IO;
    
    return TACOZ_OK;
}

/**
 * @brief Write End of Central Directory record
 * @param fp File pointer
 * @param total_entries Total number of entries
 * @param cd_size Size of central directory
 * @param cd_offset Offset of central directory
 * @return TACOZ_OK on success, error code on failure
 */
static int write_eocd(FILE *fp, uint16_t total_entries, uint32_t cd_size, uint64_t cd_offset) {
    unsigned char eocd[22];
    
    /* End of central directory signature */
    le32(eocd + 0, ZIP_EOCD_SIGNATURE);
    
    /* Disk numbers */
    le16(eocd + 4, 0);  /* number of this disk */
    le16(eocd + 6, 0);  /* disk with start of central directory */
    
    /* Entry counts */
    le16(eocd + 8, total_entries);   /* entries on this disk */
    le16(eocd + 10, total_entries);  /* total entries */
    
    /* Central directory size */
    le32(eocd + 12, cd_size);
    
    /* Central directory offset */
    if (cd_offset >= ZIP64_MARKER) {
        le32(eocd + 16, ZIP64_MARKER);  /* ZIP64 marker */
    } else {
        le32(eocd + 16, (uint32_t)cd_offset);
    }
    
    /* Comment length */
    le16(eocd + 20, 0);
    
    /* Write EOCD */
    if (fwrite(eocd, 1, 22, fp) != 22) return TACOZ_ERR_IO;
    
    return TACOZ_OK;
}

/**
 * @brief Calculate offset of ghost payload within Local File Header
 * @param fp File pointer positioned at start of file
 * @param payload_offset Output: byte offset where ghost payload starts
 * @return TACOZ_OK on success, error code on failure
 */
static int calculate_ghost_payload_offset(FILE *fp, uint64_t *payload_offset) {
    unsigned char header[30];
    
    /* Read Local File Header */
    if (fseek(fp, 0, SEEK_SET) != 0) return TACOZ_ERR_IO;
    if (fread(header, 1, 30, fp) != 30) return TACOZ_ERR_IO;
    
    /* Verify LFH signature */
    if (header[0] != 0x50 || header[1] != 0x4b || 
        header[2] != 0x03 || header[3] != 0x04) {
        return TACOZ_ERR_INVALID_GHOST;
    }
    
    /* Extract filename and extra field lengths */
    uint16_t filename_len = header[26] | (header[27] << 8);
    uint16_t extra_len = header[28] | (header[29] << 8);
    
    /* Verify filename is "TACO_GHOST" */
    if (filename_len != TACO_GHOST_NAME_LEN) {
        return TACOZ_ERR_INVALID_GHOST;
    }
    
    char filename[TACO_GHOST_NAME_LEN + 1];
    if (fread(filename, 1, TACO_GHOST_NAME_LEN, fp) != TACO_GHOST_NAME_LEN) {
        return TACOZ_ERR_IO;
    }
    filename[TACO_GHOST_NAME_LEN] = '\0';
    
    if (strcmp(filename, TACO_GHOST_NAME) != 0) {
        return TACOZ_ERR_INVALID_GHOST;
    }
    
    /* Calculate payload offset: LFH (30) + filename + extra field */
    *payload_offset = 30 + filename_len + extra_len;
    
    return TACOZ_OK;
}

/**
 * @brief Update ghost payload directly in file
 * @param fp File pointer opened for r+b
 * @param offset Byte offset where payload starts
 * @param meta_offsets Array of 7 offset values
 * @param meta_lengths Array of 7 length values
 * @return TACOZ_OK on success, error code on failure
 */
static int write_ghost_payload_direct(FILE *fp, uint64_t offset, 
                                     const uint64_t *meta_offsets, 
                                     const uint64_t *meta_lengths) {
    /* Convert arrays to metadata structure */
    taco_meta_array_t meta = {0};
    arrays_to_meta_struct(meta_offsets, meta_lengths, &meta);
    
    /* Create payload */
    unsigned char payload[TACO_GHOST_PAYLOAD_SIZE];
    create_ghost_payload(&meta, payload);
    
    /* Seek to payload position */
    if (fseek(fp, offset, SEEK_SET) != 0) {
        return TACOZ_ERR_IO;
    }
    
    /* Write payload */
    if (fwrite(payload, 1, TACO_GHOST_PAYLOAD_SIZE, fp) != TACO_GHOST_PAYLOAD_SIZE) {
        return TACOZ_ERR_IO;
    }
    
    /* Flush to ensure write is complete */
    if (fflush(fp) != 0) {
        return TACOZ_ERR_IO;
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

    /* Open file for reading and writing */
    FILE *fp = fopen(zip_path, "r+b");
    if (!fp) {
        return TACOZ_ERR_IO;
    }

    /* Calculate ghost payload offset */
    uint64_t payload_offset;
    int rc = calculate_ghost_payload_offset(fp, &payload_offset);
    if (rc != TACOZ_OK) {
        fclose(fp);
        return rc;
    }

    /* Update ghost payload directly */
    rc = write_ghost_payload_direct(fp, payload_offset, meta_offsets, meta_lengths);
    if (rc != TACOZ_OK) {
        fclose(fp);
        return rc;
    }

    fclose(fp);
    return TACOZ_OK;
}

int tacozip_append_files(const char *zip_path,
                        const tacozip_append_entry_t *entries,
                        size_t num_entries) {
    if (!zip_path || !entries || num_entries == 0) {
        return TACOZ_ERR_PARAM;
    }

    /* Validate all entries first */
    for (size_t i = 0; i < num_entries; i++) {
        if (!entries[i].src_path || !entries[i].arc_name) {
            return TACOZ_ERR_PARAM;
        }
        
        /* Protect against adding duplicate ghost entry */
        if (strcmp(entries[i].arc_name, TACO_GHOST_NAME) == 0) {
            return TACOZ_ERR_PARAM;
        }
        
        /* Verify source file exists and is readable */
        FILE *test_file = fopen(entries[i].src_path, "rb");
        if (!test_file) {
            return TACOZ_ERR_IO;
        }
        fclose(test_file);
    }

    /* Open ZIP file for reading/writing */
    FILE *fp = fopen(zip_path, "r+b");
    if (!fp) {
        return TACOZ_ERR_IO;
    }

    /* Read existing Central Directory as binary blob */
    uint64_t old_cd_offset;
    unsigned char *existing_cd_data;
    uint32_t existing_cd_size;
    uint16_t existing_count;
    int rc = read_existing_cd_blob(fp, &old_cd_offset, &existing_cd_data, &existing_cd_size, &existing_count);
    if (rc != TACOZ_OK) {
        fclose(fp);
        return rc;
    }

    /* Check for duplicate names */
    for (size_t i = 0; i < num_entries; i++) {
        /* Check against existing files */
        if (filename_exists_in_cd(existing_cd_data, existing_cd_size, entries[i].arc_name) == 1) {
            free(existing_cd_data);
            fclose(fp);
            return TACOZ_ERR_EXISTS;
        }
        
        /* Check for duplicates within the batch */
        for (size_t j = i + 1; j < num_entries; j++) {
            if (strcmp(entries[i].arc_name, entries[j].arc_name) == 0) {
                free(existing_cd_data);
                fclose(fp);
                return TACOZ_ERR_EXISTS;
            }
        }
    }

    /* Position at end of last file (start of old Central Directory) */
    if (fseeko(fp, old_cd_offset, SEEK_SET) != 0) {
        free(existing_cd_data);
        fclose(fp);
        return TACOZ_ERR_IO;
    }

    /* Track new file info for CD entries */
    typedef struct {
        uint64_t local_offset;
        uint64_t file_size;
        uint32_t crc32;
    } new_file_info_t;
    
    new_file_info_t *new_files = malloc(num_entries * sizeof(new_file_info_t));
    if (!new_files) {
        free(existing_cd_data);
        fclose(fp);
        return TACOZ_ERR_IO;
    }

    /* Append each file */
    for (size_t i = 0; i < num_entries; i++) {
        /* Record local header offset */
        new_files[i].local_offset = ftello(fp);
        
        FILE *src_fp = fopen(entries[i].src_path, "rb");
        if (!src_fp) {
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return TACOZ_ERR_IO;
        }

        /* Get file size */
        struct stat st;
        if (stat(entries[i].src_path, &st) != 0) {
            fclose(src_fp);
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return TACOZ_ERR_IO;
        }

        /* Write local file header with temporary CRC=0 */
        rc = write_local_file_header(fp, entries[i].arc_name, st.st_size, 0);
        if (rc != TACOZ_OK) {
            fclose(src_fp);
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return rc;
        }

        /* Copy file and calculate CRC32 */
        rc = copy_file_with_crc(src_fp, fp, &new_files[i].crc32, &new_files[i].file_size);
        fclose(src_fp);
        
        if (rc != TACOZ_OK) {
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return rc;
        }

        /* Go back and update the CRC32 in the local file header */
        off_t current_pos = ftello(fp);
        off_t crc_offset = new_files[i].local_offset + 14;  /* CRC32 offset in LFH */
        
        if (fseeko(fp, crc_offset, SEEK_SET) != 0) {
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return TACOZ_ERR_IO;
        }
        
        unsigned char crc_bytes[4];
        le32(crc_bytes, new_files[i].crc32);
        if (fwrite(crc_bytes, 1, 4, fp) != 4) {
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return TACOZ_ERR_IO;
        }
        
        /* Return to end of file */
        if (fseeko(fp, current_pos, SEEK_SET) != 0) {
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return TACOZ_ERR_IO;
        }
    }

    /* Now write new Central Directory */
    uint64_t new_cd_offset = ftello(fp);
    
    /* First: write existing CD data as-is */
    if (fwrite(existing_cd_data, 1, existing_cd_size, fp) != existing_cd_size) {
        free(new_files);
        free(existing_cd_data);
        fclose(fp);
        return TACOZ_ERR_IO;
    }
    
    /* Second: write new CD entries */
    uint32_t new_entries_size = 0;
    for (size_t i = 0; i < num_entries; i++) {
        off_t start_pos = ftello(fp);
        rc = write_cd_entry(fp, entries[i].arc_name,
                           new_files[i].local_offset,
                           new_files[i].file_size,
                           new_files[i].crc32);
        if (rc != TACOZ_OK) {
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return rc;
        }
        new_entries_size += ftello(fp) - start_pos;
    }

    /* Write new EOCD record */
    uint16_t total_entries = existing_count + num_entries;
    uint32_t total_cd_size = existing_cd_size + new_entries_size;
    rc = write_eocd(fp, total_entries, total_cd_size, new_cd_offset);
    if (rc != TACOZ_OK) {
        free(new_files);
        free(existing_cd_data);
        fclose(fp);
        return rc;
    }

    /* Truncate file at current position (removes old EOCD) */
    if (ftruncate(fileno(fp), ftello(fp)) != 0) {
        free(new_files);
        free(existing_cd_data);
        fclose(fp);
        return TACOZ_ERR_IO;
    }

    /* Cleanup */
    free(new_files);
    free(existing_cd_data);
    fclose(fp);
    
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