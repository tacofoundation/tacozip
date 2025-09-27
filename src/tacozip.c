/*
 * tacozip.c — ZIP64 (STORE-only) writer with libzip backend and TACO Header supporting up to 7 metadata entries.
 *
 * This simplified implementation provides a clean API with only the essential functions:
 * - tacozip_create() - create archive with up to 7 metadata entries
 * - tacozip_update_header() - write/update header metadata (optimized, bypasses libzip)
 * - tacozip_read_header() - read header metadata (optimized, bypasses libzip)
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
/* Windows-specific defines for file operations */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
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
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define fileno _fileno
#define ftruncate(fd, size) _chsize_s(fd, size)
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

/* ----------------------- Timestamp conversion functions ------------------- */

/**
 * @brief Convert Unix timestamp to ZIP MS-DOS format (cross-platform)
 * @param unix_time Unix timestamp from stat()
 * @param dos_time Output: MS-DOS time (16-bit)
 * @param dos_date Output: MS-DOS date (16-bit) 
 */
static void unix_time_to_dos(time_t unix_time, uint16_t *dos_time, uint16_t *dos_date) {
    struct tm *tm_info = localtime(&unix_time);
    
    if (!tm_info) {
        /* Fallback to dummy values if conversion fails */
        *dos_time = 0;
        *dos_date = 0;
        return;
    }
    
    /* MS-DOS time: bits 15-11=hour, 10-5=minute, 4-0=second/2 */
    *dos_time = ((tm_info->tm_hour & 0x1F) << 11) |
                ((tm_info->tm_min & 0x3F) << 5) |
                ((tm_info->tm_sec / 2) & 0x1F);
    
    /* MS-DOS date: bits 15-9=year-1980, 8-5=month, 4-0=day */
    int year = tm_info->tm_year + 1900;
    if (year < 1980) year = 1980;  /* ZIP minimum year */
    
    *dos_date = (((year - 1980) & 0x7F) << 9) |
                (((tm_info->tm_mon + 1) & 0x0F) << 5) |
                (tm_info->tm_mday & 0x1F);
}

/* ----------------------- Metadata helper functions ------------------------- */

/**
 * @brief Create header payload from metadata structure.
 * @param meta Input metadata structure
 * @param payload Output buffer (must be at least TACO_HEADER_PAYLOAD_SIZE bytes)
 */
static void create_header_payload(const taco_meta_array_t *meta, unsigned char *payload) {
    memset(payload, 0, TACO_HEADER_PAYLOAD_SIZE);
    
    /* Count byte + 3 padding bytes for alignment */
    payload[0] = meta->count;
    payload[1] = payload[2] = payload[3] = 0;  /* padding */

    /* 7 pairs of (offset, length) - 112 bytes total */
    unsigned char *pairs_start = payload + 4;
    for (size_t i = 0; i < TACO_HEADER_MAX_ENTRIES; i++) {
        le64(pairs_start + i * 16 + 0, meta->entries[i].offset);
        le64(pairs_start + i * 16 + 8, meta->entries[i].length);
    }
}

/**
 * @brief Parse header payload into metadata structure.
 * @param payload Input buffer (must be exactly TACO_HEADER_PAYLOAD_SIZE bytes)
 * @param meta Output metadata structure
 */
static void parse_header_payload(const unsigned char *payload, taco_meta_array_t *meta) {
    /* Extract count byte */
    meta->count = payload[0];
    
    /* Clamp count to valid range */
    if (meta->count > TACO_HEADER_MAX_ENTRIES) {
        meta->count = TACO_HEADER_MAX_ENTRIES;
    }

    /* Parse 7 pairs of (offset, length) - 112 bytes total */
    const unsigned char *pairs_start = payload + 4;
    for (size_t i = 0; i < TACO_HEADER_MAX_ENTRIES; i++) {
        /* Read little-endian uint64_t values */
        uint64_t offset = 0, length = 0;
        
        for (int j = 0; j < 8; j++) {
            offset |= ((uint64_t)pairs_start[i * 16 + j]) << (j * 8);
            length |= ((uint64_t)pairs_start[i * 16 + 8 + j]) << (j * 8);
        }
        
        meta->entries[i].offset = offset;
        meta->entries[i].length = length;
    }
}

/* Central Directory entry info for trim operations */
typedef struct {
    uint32_t cd_entry_offset;  /* Offset within CD data */
    uint64_t local_offset;     /* Physical offset in ZIP file */
    uint16_t filename_len;
    char *filename;
    int matches_target;
} cd_entry_info_t;

/* Helper function for clean memory cleanup
 * @param entries Array of cd_entry_info_t
 * @param count Number of entries in the array
*/
static void cleanup_cd_entries(cd_entry_info_t *entries, uint16_t count) {
    if (!entries) return;
    for (uint16_t i = 0; i < count; i++) {
        free(entries[i].filename);
    }
    free(entries);
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
 * @brief Add header entry to libzip archive.
 * @param za libzip archive handle
 * @param meta Metadata structure for header payload
 * @return TACOZ_OK on success, error code on failure
 */
static int add_header_to_archive(zip_t *za, const taco_meta_array_t *meta) {
    /* Create header payload */
    unsigned char *payload = malloc(TACO_HEADER_PAYLOAD_SIZE);
    if (!payload) return TACOZ_ERR_IO;
    
    create_header_payload(meta, payload);

    /* Create source from buffer */
    zip_source_t *source = zip_source_buffer(za, payload, TACO_HEADER_PAYLOAD_SIZE, 1); /* 1 = freep */
    if (!source) {
        free(payload);
        return TACOZ_ERR_LIBZIP;
    }

    /* Add header entry to archive */
    zip_int64_t index = zip_file_add(za, TACO_HEADER_NAME, source, ZIP_FL_OVERWRITE);
    if (index < 0) {
        zip_source_free(source);
        return TACOZ_ERR_LIBZIP;
    }

    /* Set file attributes to identify as custom binary format */
    zip_uint32_t external_attr = 0644 << 16;  /* Regular file permissions in high 16 bits */
    if (zip_file_set_external_attributes(za, (zip_uint64_t)index, 
                                        ZIP_FL_UNCHANGED, ZIP_OPSYS_UNIX, external_attr) < 0) {
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
    if (file_size < EOCD_MIN_SIZE) return TACOZ_ERR_INVALID_HEADER;
    
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
    return TACOZ_ERR_INVALID_HEADER;
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
 * @brief Write a local file header for STORE method with real timestamps
 * @param fp File pointer positioned where to write LFH
 * @param filename Archive filename
 * @param file_size Uncompressed file size
 * @param crc32 CRC32 checksum
 * @param mtime File modification time (Unix timestamp)
 * @return TACOZ_OK on success, error code on failure
 */
static int write_local_file_header(FILE *fp, const char *filename, uint64_t file_size, 
                                  uint32_t crc32, time_t mtime) {
    unsigned char header[30];
    uint16_t filename_len = (uint16_t)strlen(filename);
    
    /* Convert Unix timestamp to DOS format */
    uint16_t dos_time, dos_date;
    unix_time_to_dos(mtime, &dos_time, &dos_date);
    
    /* Local file header signature */
    le32(header + 0, ZIP_LFH_SIGNATURE);
    
    /* Version needed to extract */
    le16(header + 4, ZIP_VERSION_NEEDED_ZIP64);
    
    /* General purpose bit flag */
    le16(header + 6, TACOZ_SET_UTF8_FLAG ? (1 << 11) : 0);
    
    /* Compression method (0 = STORE) */
    le16(header + 8, 0);
    
    /* File last modification time & date (real timestamps) */
    le16(header + 10, dos_time);
    le16(header + 12, dos_date);
    
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
 * @brief Write a Central Directory entry with real timestamps and attributes
 * @param fp File pointer
 * @param filename Archive filename  
 * @param local_offset Offset of local file header
 * @param file_size File size
 * @param crc32 CRC32 checksum
 * @param mtime File modification time (Unix timestamp)
 * @param file_mode File mode/permissions from stat()
 * @return TACOZ_OK on success, error code on failure
 */
static int write_cd_entry(FILE *fp, const char *filename, uint64_t local_offset, 
                         uint64_t file_size, uint32_t crc32, time_t mtime, mode_t file_mode) {
    unsigned char header[46];
    uint16_t filename_len = (uint16_t)strlen(filename);
    
    /* Convert Unix timestamp to DOS format */
    uint16_t dos_time, dos_date;
    unix_time_to_dos(mtime, &dos_time, &dos_date);
    
    /* Central directory file header signature */
    le32(header + 0, ZIP_CDH_SIGNATURE);
    
    /* Version made by (Unix system) */
    le16(header + 4, (ZIP_OPSYS_UNIX << 8) | ZIP_VERSION_NEEDED_ZIP64);
    
    /* Version needed to extract */
    le16(header + 6, ZIP_VERSION_NEEDED_ZIP64);
    
    /* General purpose bit flag */
    le16(header + 8, TACOZ_SET_UTF8_FLAG ? (1 << 11) : 0);
    
    /* Compression method (0 = STORE) */
    le16(header + 10, 0);
    
    /* File last modification time & date (real timestamps) */
    le16(header + 12, dos_time);
    le16(header + 14, dos_date);
    
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
    
    /* External file attributes - preserve original file permissions */
    le32(header + 38, (file_mode & 0xFFFF) << 16);  /* Unix permissions in high 16 bits */
    
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
 * @brief Calculate offset of header payload within Local File Header
 * @param fp File pointer positioned at start of file
 * @param payload_offset Output: byte offset where header payload starts
 * @return TACOZ_OK on success, error code on failure
 */
static int calculate_header_payload_offset(FILE *fp, uint64_t *payload_offset) {
    unsigned char header[30];
    
    /* Read Local File Header */
    if (fseek(fp, 0, SEEK_SET) != 0) return TACOZ_ERR_IO;
    if (fread(header, 1, 30, fp) != 30) return TACOZ_ERR_IO;
    
    /* Verify LFH signature */
    if (header[0] != 0x50 || header[1] != 0x4b || 
        header[2] != 0x03 || header[3] != 0x04) {
        return TACOZ_ERR_INVALID_HEADER;
    }
    
    /* Extract filename and extra field lengths */
    uint16_t filename_len = header[26] | (header[27] << 8);
    uint16_t extra_len = header[28] | (header[29] << 8);
    
    /* Verify filename is "TACO_HEADER" */
    if (filename_len != TACO_HEADER_NAME_LEN) {
        return TACOZ_ERR_INVALID_HEADER;
    }
    
    char filename[TACO_HEADER_NAME_LEN + 1];
    if (fread(filename, 1, TACO_HEADER_NAME_LEN, fp) != TACO_HEADER_NAME_LEN) {
        return TACOZ_ERR_IO;
    }
    filename[TACO_HEADER_NAME_LEN] = '\0';
    
    if (strcmp(filename, TACO_HEADER_NAME) != 0) {
        return TACOZ_ERR_INVALID_HEADER;
    }
    
    /* Calculate payload offset: LFH (30) + filename + extra field */
    *payload_offset = 30 + filename_len + extra_len;
    
    return TACOZ_OK;
}


/**
 * @brief Find and update CRC32 in Central Directory entry for TACO_HEADER
 * @param fp File pointer opened for r+b
 * @param new_crc32 New CRC32 value to write
 * @return TACOZ_OK on success, error code on failure
 */
static int update_header_cd_crc32(FILE *fp, uint32_t new_crc32) {
    /* Read existing Central Directory blob */
    uint64_t cd_offset;
    unsigned char *cd_data;
    uint32_t cd_size;
    uint16_t total_entries;
    
    int rc = read_existing_cd_blob(fp, &cd_offset, &cd_data, &cd_size, &total_entries);
    if (rc != TACOZ_OK) {
        return rc;
    }
    
    /* Search for TACO_HEADER entry in CD */
    uint32_t offset = 0;
    int found = 0;
    uint32_t header_cd_offset = 0;
    
    while (offset < cd_size && !found) {
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
        
        /* Compare filename with TACO_HEADER */
        if (filename_len == TACO_HEADER_NAME_LEN && 
            memcmp(cd_data + offset + 46, TACO_HEADER_NAME, TACO_HEADER_NAME_LEN) == 0) {
            header_cd_offset = offset;
            found = 1;
            break;
        }
        
        /* Move to next entry */
        offset += 46 + filename_len + extra_len + comment_len;
    }
    
    if (!found) {
        free(cd_data);
        return TACOZ_ERR_NOT_FOUND;
    }
    
    /* Update CRC32 in the CD entry (offset 16 within CD entry) */
    uint64_t file_crc_offset = cd_offset + header_cd_offset + 16;
    
    if (fseeko(fp, file_crc_offset, SEEK_SET) != 0) {
        free(cd_data);
        return TACOZ_ERR_IO;
    }
    
    unsigned char crc_bytes[4];
    le32(crc_bytes, new_crc32);
    if (fwrite(crc_bytes, 1, 4, fp) != 4) {
        free(cd_data);
        return TACOZ_ERR_IO;
    }
    
    free(cd_data);
    return TACOZ_OK;
}


/**
 * @brief Update header payload directly in file
 * @param fp File pointer opened for r+b
 * @param offset Byte offset where payload starts
 * @param meta Metadata structure containing entries to write
 * @return TACOZ_OK on success, error code on failure
 */
static int write_header_payload_direct(FILE *fp, uint64_t offset, const taco_meta_array_t *meta) {
    /* Create payload */
    unsigned char payload[TACO_HEADER_PAYLOAD_SIZE];
    create_header_payload(meta, payload);
    
    /* Seek to payload position */
    if (fseek(fp, offset, SEEK_SET) != 0) {
        return TACOZ_ERR_IO;
    }
    
    /* Write payload */
    if (fwrite(payload, 1, TACO_HEADER_PAYLOAD_SIZE, fp) != TACO_HEADER_PAYLOAD_SIZE) {
        return TACOZ_ERR_IO;
    }
    
    /* Calculate CRC32 of the new payload */
    uLong new_crc = crc32(0L, Z_NULL, 0);
    new_crc = crc32(new_crc, payload, TACO_HEADER_PAYLOAD_SIZE);
    uint32_t final_crc = (uint32_t)new_crc;
    
    /* Update Local File Header CRC32 (always at offset 14) */
    if (fseek(fp, 14, SEEK_SET) != 0) {
        return TACOZ_ERR_IO;
    }
    
    unsigned char crc_bytes[4];
    le32(crc_bytes, final_crc);
    if (fwrite(crc_bytes, 1, 4, fp) != 4) {
        return TACOZ_ERR_IO;
    }
    
    /* Update Central Directory entry CRC32 */
    int rc = update_header_cd_crc32(fp, final_crc);
    if (rc != TACOZ_OK) {
        return rc;
    }
    
    /* Flush to ensure all writes are complete */
    if (fflush(fp) != 0) {
        return TACOZ_ERR_IO;
    }
    
    return TACOZ_OK;
}

/**
 * @brief Read header payload directly from file
 * @param fp File pointer opened for reading
 * @param offset Byte offset where payload starts
 * @param meta Output metadata structure
 * @return TACOZ_OK on success, error code on failure
 */
static int read_header_payload_direct(FILE *fp, uint64_t offset, taco_meta_array_t *meta) {
    /* Seek to payload position */
    if (fseek(fp, offset, SEEK_SET) != 0) {
        return TACOZ_ERR_IO;
    }
    
    /* Read payload */
    unsigned char payload[TACO_HEADER_PAYLOAD_SIZE];
    if (fread(payload, 1, TACO_HEADER_PAYLOAD_SIZE, fp) != TACO_HEADER_PAYLOAD_SIZE) {
        return TACOZ_ERR_IO;
    }
    
    /* Parse payload into structure */
    parse_header_payload(payload, meta);
    
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
                  const taco_meta_array_t *meta)
{
    TACOZIP_DEBUG(TACOZIP_LOG_INIT, "Creating archive '%s' with %zu files", zip_path, num_files);
    
    if (!zip_path || !src_files || !arc_files || num_files == 0 || !meta)
        return TACOZ_ERR_PARAM;

    int error;
    zip_t *za = zip_open(zip_path, ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!za) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to create archive: libzip error %d", error);
        return TACOZ_ERR_IO;
    }

    TACOZIP_DEBUG(TACOZIP_LOG_HEADER, "Header metadata: %u valid entries", meta->count);

    /* Add header entry first (so it appears at the beginning physically) */
    int rc = add_header_to_archive(za, meta);
    if (rc != TACOZ_OK) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to add header entry: %d", rc);
        zip_close(za);
        return rc;
    }
    TACOZIP_DEBUG(TACOZIP_LOG_HEADER, "Header entry added successfully");

    /* Add each regular file */
    for (size_t i = 0; i < num_files; i++) {
        if (!src_files[i] || !arc_files[i]) {
            zip_close(za);
            return TACOZ_ERR_PARAM;
        }
        
        TACOZIP_DEBUG(TACOZIP_LOG_LIBZIP, "Adding file %zu/%zu: %s -> %s", i+1, num_files, src_files[i], arc_files[i]);
        rc = add_file_to_archive(za, src_files[i], arc_files[i]);
        if (rc != TACOZ_OK) {
            TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to add file '%s': %d", src_files[i], rc);
            zip_close(za);
            return rc;
        }
    }

    /* Close and finalize the archive */
    TACOZIP_DEBUG(TACOZIP_LOG_LIBZIP, "Finalizing archive");
    if (zip_close(za) < 0) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to finalize archive");
        return TACOZ_ERR_IO;
    }

    TACOZIP_DEBUG(TACOZIP_LOG_INIT, "Archive created successfully");
    return TACOZ_OK;
}

int tacozip_update_header(const char *zip_path,
                        const taco_meta_array_t *meta) {
    TACOZIP_DEBUG(TACOZIP_LOG_HEADER, "Updating header metadata in '%s'", zip_path);
    
    if (!zip_path || !meta)
        return TACOZ_ERR_PARAM;

    /* Open file for reading and writing */
    FILE *fp = fopen(zip_path, "r+b");
    if (!fp) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to open file for header update: %s", strerror(errno));
        return TACOZ_ERR_IO;
    }

    /* Calculate header payload offset */
    uint64_t payload_offset;
    int rc = calculate_header_payload_offset(fp, &payload_offset);
    if (rc != TACOZ_OK) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to calculate header payload offset: %d", rc);
        fclose(fp);
        return rc;
    }
    TACOZIP_DEBUG(TACOZIP_LOG_HEADER, "Header payload offset: %llu", (unsigned long long)payload_offset);

    /* Update header payload directly */
    rc = write_header_payload_direct(fp, payload_offset, meta);
    if (rc != TACOZ_OK) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to write header payload: %d", rc);
        fclose(fp);
        return rc;
    }

    fclose(fp);
    TACOZIP_DEBUG(TACOZIP_LOG_HEADER, "Header metadata updated successfully");
    return TACOZ_OK;
}

int tacozip_read_header(const char *zip_path,
                      taco_meta_array_t *meta_out) {
    TACOZIP_DEBUG(TACOZIP_LOG_HEADER, "Reading header metadata from '%s'", zip_path);
    
    if (!zip_path || !meta_out) {
        return TACOZ_ERR_PARAM;
    }

    /* Initialize output structure */
    memset(meta_out, 0, sizeof(taco_meta_array_t));

    /* Open file for reading */
    FILE *fp = fopen(zip_path, "rb");
    if (!fp) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to open file for header read: %s", strerror(errno));
        return TACOZ_ERR_IO;
    }

    /* Calculate header payload offset */
    uint64_t payload_offset;
    int rc = calculate_header_payload_offset(fp, &payload_offset);
    if (rc != TACOZ_OK) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to calculate header payload offset: %d", rc);
        fclose(fp);
        return rc;
    }
    TACOZIP_DEBUG(TACOZIP_LOG_HEADER, "Header payload offset: %llu", (unsigned long long)payload_offset);

    /* Read header payload directly */
    rc = read_header_payload_direct(fp, payload_offset, meta_out);
    if (rc != TACOZ_OK) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to read header payload: %d", rc);
        fclose(fp);
        return rc;
    }

    fclose(fp);
    TACOZIP_DEBUG(TACOZIP_LOG_HEADER, "Header metadata read successfully: %u valid entries", meta_out->count);
    return TACOZ_OK;
}


int tacozip_append_files(const char *zip_path,
                        const tacozip_append_entry_t *entries,
                        size_t num_entries) {
    TACOZIP_DEBUG(TACOZIP_LOG_APPEND, "Appending %zu files to '%s'", num_entries, zip_path);
    
    if (!zip_path || !entries || num_entries == 0) {
        return TACOZ_ERR_PARAM;
    }

    /* Validate all entries first */
    for (size_t i = 0; i < num_entries; i++) {
        if (!entries[i].src_path || !entries[i].arc_name) {
            return TACOZ_ERR_PARAM;
        }
        
        /* Protect against adding duplicate header entry */
        if (strcmp(entries[i].arc_name, TACO_HEADER_NAME) == 0) {
            return TACOZ_ERR_PARAM;
        }
        
        /* Verify source file exists and is readable */
        FILE *test_file = fopen(entries[i].src_path, "rb");
        if (!test_file) {
            TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Source file not accessible: %s", entries[i].src_path);
            return TACOZ_ERR_IO;
        }
        fclose(test_file);
    }

    /* Open ZIP file for reading/writing */
    FILE *fp = fopen(zip_path, "r+b");
    if (!fp) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to open archive for append: %s", strerror(errno));
        return TACOZ_ERR_IO;
    }

    /* Read existing Central Directory as binary blob */
    uint64_t old_cd_offset;
    unsigned char *existing_cd_data;
    uint32_t existing_cd_size;
    uint16_t existing_count;
    int rc = read_existing_cd_blob(fp, &old_cd_offset, &existing_cd_data, &existing_cd_size, &existing_count);
    if (rc != TACOZ_OK) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to read existing Central Directory: %d", rc);
        fclose(fp);
        return rc;
    }
    TACOZIP_DEBUG(TACOZIP_LOG_CD, "Read existing CD: %u entries, size=%u, offset=%llu", 
                  existing_count, existing_cd_size, (unsigned long long)old_cd_offset);

    /* Check for duplicate names */
    for (size_t i = 0; i < num_entries; i++) {
        /* Check against existing files */
        if (filename_exists_in_cd(existing_cd_data, existing_cd_size, entries[i].arc_name) == 1) {
            TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Duplicate filename: %s", entries[i].arc_name);
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
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to seek to CD offset: %s", strerror(errno));
        free(existing_cd_data);
        fclose(fp);
        return TACOZ_ERR_IO;
    }
    TACOZIP_DEBUG(TACOZIP_LOG_APPEND, "Positioned at CD offset for file appending");

    /* Track new file info for CD entries */
    typedef struct {
        uint64_t local_offset;
        uint64_t file_size;
        uint32_t crc32;
        time_t mtime;
        mode_t file_mode;
    } new_file_info_t;
    
    new_file_info_t *new_files = malloc(num_entries * sizeof(new_file_info_t));
    if (!new_files) {
        free(existing_cd_data);
        fclose(fp);
        return TACOZ_ERR_IO;
    }

    /* Append each file */
    for (size_t i = 0; i < num_entries; i++) {
        TACOZIP_DEBUG(TACOZIP_LOG_IO, "Writing file %zu/%zu: %s -> %s", 
                      i+1, num_entries, entries[i].src_path, entries[i].arc_name);
        
        /* Record local header offset */
        new_files[i].local_offset = ftello(fp);
        
        FILE *src_fp = fopen(entries[i].src_path, "rb");
        if (!src_fp) {
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return TACOZ_ERR_IO;
        }

        /* Get file stats - size, mtime, and permissions */
        struct stat st;
        if (stat(entries[i].src_path, &st) != 0) {
            fclose(src_fp);
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return TACOZ_ERR_IO;
        }
        
        /* Store file metadata */
        new_files[i].mtime = st.st_mtime;
        new_files[i].file_mode = st.st_mode;

        /* Write local file header with temporary CRC=0 but real timestamp */
        rc = write_local_file_header(fp, entries[i].arc_name, st.st_size, 0, st.st_mtime);
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
        
        TACOZIP_DEBUG(TACOZIP_LOG_IO, "File written: size=%llu, CRC32=0x%08x", 
                      (unsigned long long)new_files[i].file_size, new_files[i].crc32);
    }

    /* Now write new Central Directory */
    uint64_t new_cd_offset = ftello(fp);
    TACOZIP_DEBUG(TACOZIP_LOG_CD, "Writing new Central Directory at offset %llu", 
                  (unsigned long long)new_cd_offset);
    
    /* First: write existing CD data as-is */
    if (fwrite(existing_cd_data, 1, existing_cd_size, fp) != existing_cd_size) {
        free(new_files);
        free(existing_cd_data);
        fclose(fp);
        return TACOZ_ERR_IO;
    }
    
    /* Second: write new CD entries with proper timestamps and attributes */
    uint32_t new_entries_size = 0;
    for (size_t i = 0; i < num_entries; i++) {
        off_t start_pos = ftello(fp);
        rc = write_cd_entry(fp, entries[i].arc_name,
                           new_files[i].local_offset,
                           new_files[i].file_size,
                           new_files[i].crc32,
                           new_files[i].mtime,
                           new_files[i].file_mode);
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
    
    TACOZIP_DEBUG(TACOZIP_LOG_APPEND, "Successfully appended %zu files, total entries now: %u", 
                  num_entries, total_entries);
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

    /* Protect against replacing the header entry */
    if (strcmp(file_name, TACO_HEADER_NAME) == 0) {
        zip_close(za);
        return TACOZ_ERR_PARAM;  /* Cannot replace header with this function */
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



/**
 * @brief Trim archive from a specific point to the end (METADATA/ or COLLECTION.json only)
 * 
 * This function removes the specified target and everything after it in the physical
 * archive layout. Only supports "METADATA/" (directory and all contents) and 
 * "COLLECTION.json" (single file) for safety.
 * 
 * @param zip_path Path to existing TACO archive
 * @param target Either "METADATA/" or "COLLECTION.json"
 * @return TACOZ_OK on success, error code on failure
 */
int tacozip_trim_from(const char *zip_path, const char *target) {
    TACOZIP_DEBUG(TACOZIP_LOG_INIT, "Trimming archive '%s' from target '%s'", zip_path, target);
    
    if (!zip_path || !target) {
        return TACOZ_ERR_PARAM;
    }
    
    /* Strict whitelist validation */
    int is_metadata = (strcmp(target, "METADATA/") == 0);
    int is_collection = (strcmp(target, "COLLECTION.json") == 0);
    
    if (!is_metadata && !is_collection) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Invalid target '%s' - only 'METADATA/' or 'COLLECTION.json' allowed", target);
        return TACOZ_ERR_PARAM;
    }
    
    /* Open archive for reading and writing */
    FILE *fp = fopen(zip_path, "r+b");
    if (!fp) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to open archive: %s", strerror(errno));
        return TACOZ_ERR_IO;
    }
    
    /* Read existing Central Directory */
    uint64_t cd_offset;
    unsigned char *cd_data = NULL;
    uint32_t cd_size;
    uint16_t total_entries;
    
    int rc = read_existing_cd_blob(fp, &cd_offset, &cd_data, &cd_size, &total_entries);
    if (rc != TACOZ_OK) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to read Central Directory: %d", rc);
        fclose(fp);
        return rc;
    }
    
    TACOZIP_DEBUG(TACOZIP_LOG_CD, "Read CD: %u entries, size=%u", total_entries, cd_size);
    
    /* Parse Central Directory to find matching entries and their physical positions */
    cd_entry_info_t *entries = malloc(total_entries * sizeof(cd_entry_info_t));
    memset(entries, 0, total_entries * sizeof(cd_entry_info_t));
    
    /* Parse all CD entries */
    uint32_t cd_offset_iter = 0;
    uint16_t parsed_entries = 0;
    uint64_t trim_start_offset = UINT64_MAX;  /* Earliest offset of files to remove */
    uint16_t matching_entries = 0;
    
    while (cd_offset_iter < cd_size && parsed_entries < total_entries) {
        if (cd_offset_iter + 46 > cd_size) break;
        
        /* Verify CD signature */
        if (cd_data[cd_offset_iter] != 0x50 || cd_data[cd_offset_iter+1] != 0x4b ||
            cd_data[cd_offset_iter+2] != 0x01 || cd_data[cd_offset_iter+3] != 0x02) {
            break;
        }
        
        /* Extract field lengths */
        uint16_t filename_len = cd_data[cd_offset_iter+28] | (cd_data[cd_offset_iter+29] << 8);
        uint16_t extra_len = cd_data[cd_offset_iter+30] | (cd_data[cd_offset_iter+31] << 8);
        uint16_t comment_len = cd_data[cd_offset_iter+32] | (cd_data[cd_offset_iter+33] << 8);
        
        if (cd_offset_iter + 46 + filename_len > cd_size) break;
        
        /* Extract local header offset */
        uint32_t local_offset_32 = cd_data[cd_offset_iter+42] | (cd_data[cd_offset_iter+43] << 8) | 
                                   (cd_data[cd_offset_iter+44] << 16) | (cd_data[cd_offset_iter+45] << 24);
        
        /* Handle ZIP64 offsets properly */
        uint64_t local_offset;
        if (local_offset_32 == ZIP64_MARKER) {
            /* Skip ZIP64 handling for now - this would require parsing extra fields */
            TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "ZIP64 local header offsets not yet supported");
            rc = TACOZ_ERR_INVALID_HEADER;
            goto cleanup;
        } else {
            local_offset = local_offset_32;
        }
        
        /* Store entry info */
        entries[parsed_entries].cd_entry_offset = cd_offset_iter;
        entries[parsed_entries].local_offset = local_offset;
        entries[parsed_entries].filename_len = filename_len;
        entries[parsed_entries].filename = malloc(filename_len + 1);
        if (!entries[parsed_entries].filename) {
            rc = TACOZ_ERR_IO;
            goto cleanup;
        }
        
        memcpy(entries[parsed_entries].filename, cd_data + cd_offset_iter + 46, filename_len);
        entries[parsed_entries].filename[filename_len] = '\0';
        
        /* Check if this entry matches our target */
        int matches = 0;
        if (is_metadata) {
            /* For METADATA/, match files that start with "METADATA/" */
            matches = (strncmp(entries[parsed_entries].filename, "METADATA/", 9) == 0);
        } else if (is_collection) {
            /* For COLLECTION.json, exact match */
            matches = (strcmp(entries[parsed_entries].filename, "COLLECTION.json") == 0);
        }
        
        entries[parsed_entries].matches_target = matches;
        
        if (matches) {
            matching_entries++;
            if (local_offset < trim_start_offset) {
                trim_start_offset = local_offset;
            }
            TACOZIP_DEBUG(TACOZIP_LOG_CD, "Found matching entry: '%s' at offset %llu", 
                         entries[parsed_entries].filename, (unsigned long long)local_offset);
        }
        
        parsed_entries++;
        
        /* Move to next CD entry */
        cd_offset_iter += 46 + filename_len + extra_len + comment_len;
    }
    
    /* Validation: ensure we found matching entries */
    if (matching_entries == 0) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Target '%s' not found in archive", target);
        rc = TACOZ_ERR_NOT_FOUND;
        goto cleanup;
    }
    
    /* Validate trim_start_offset is reasonable */
    if (trim_start_offset == UINT64_MAX) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Invalid trim offset calculated");
        rc = TACOZ_ERR_INVALID_HEADER;
        goto cleanup;
    }
    
    TACOZIP_DEBUG(TACOZIP_LOG_CD, "Found %u matching entries, trim_start_offset=%llu", 
                  matching_entries, (unsigned long long)trim_start_offset);
    
    /* Critical validation: ensure no non-matching files exist after trim point */
    for (uint16_t i = 0; i < parsed_entries; i++) {
        if (!entries[i].matches_target && entries[i].local_offset >= trim_start_offset) {
            TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Cannot trim: file '%s' exists after trim point", entries[i].filename);
            rc = TACOZ_ERR_PARAM;  /* Operation not safe */
            goto cleanup;
        }
    }
    
    /* Truncate the ZIP file at the trim point */
    TACOZIP_DEBUG(TACOZIP_LOG_IO, "Truncating file at offset %llu", (unsigned long long)trim_start_offset);
    if (ftruncate(fileno(fp), trim_start_offset) != 0) {
        TACOZIP_DEBUG(TACOZIP_LOG_ERROR, "Failed to truncate file: %s", strerror(errno));
        rc = TACOZ_ERR_IO;
        goto cleanup;
    }
    
    /* Position at the new end of file for writing new CD */
    if (fseeko(fp, trim_start_offset, SEEK_SET) != 0) {
        rc = TACOZ_ERR_IO;
        goto cleanup;
    }
    
    uint64_t new_cd_offset = trim_start_offset;
    
    /* Write new Central Directory with only remaining entries */
    uint16_t remaining_entries = 0;
    uint32_t new_cd_size = 0;
    
    for (uint16_t i = 0; i < parsed_entries; i++) {
        if (!entries[i].matches_target) {
            /* Copy this CD entry as-is from original CD data */
            uint32_t entry_start = entries[i].cd_entry_offset;
            
            /* Find entry size by looking at filename, extra, comment lengths */
            uint16_t filename_len = cd_data[entry_start+28] | (cd_data[entry_start+29] << 8);
            uint16_t extra_len = cd_data[entry_start+30] | (cd_data[entry_start+31] << 8);
            uint16_t comment_len = cd_data[entry_start+32] | (cd_data[entry_start+33] << 8);
            uint32_t entry_size = 46 + filename_len + extra_len + comment_len;
            
            if (fwrite(cd_data + entry_start, 1, entry_size, fp) != entry_size) {
                rc = TACOZ_ERR_IO;
                goto cleanup;
            }
            
            new_cd_size += entry_size;
            remaining_entries++;
            
            TACOZIP_DEBUG(TACOZIP_LOG_CD, "Kept entry: '%s'", entries[i].filename);
        }
    }
    
    /* Write new EOCD */
    rc = write_eocd(fp, remaining_entries, new_cd_size, new_cd_offset);
    if (rc != TACOZ_OK) {
        goto cleanup;
    }
    
    TACOZIP_DEBUG(TACOZIP_LOG_INIT, "Trim completed: removed %u entries, %u remain", 
                  matching_entries, remaining_entries);
    
    rc = TACOZ_OK;

cleanup:
    cleanup_cd_entries(entries, parsed_entries);
    free(cd_data);
    if (fp) fclose(fp);
    return rc;
}