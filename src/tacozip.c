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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
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

#include <zip.h>
#include <zlib.h>

/* ------------------------------- Debug System ------------------------------ */
static int tacozip_debug_enabled = -1;

static inline int tacozip_should_debug(void) {
    if (tacozip_debug_enabled == -1) {
        const char *env = getenv("TACOZIP_DEBUG");
        tacozip_debug_enabled = (env && (
            strcmp(env, "1") == 0 || 
            strcmp(env, "ON") == 0 || 
            strcmp(env, "TRUE") == 0
        )) ? 1 : 0;
    }
    return tacozip_debug_enabled;
}

#define TACOZIP_DEBUG(msg, ...) do { \
    if (tacozip_should_debug()) { \
        fprintf(stderr, "[TACOZIP] " msg "\n", ##__VA_ARGS__); \
    } \
} while(0)

/* ------------------------------- Tunables ---------------------------------- */
#ifndef TACOZ_COPY_BUFSZ
#define TACOZ_COPY_BUFSZ (1u << 20)
#endif
#ifndef TACOZ_SET_UTF8_FLAG
#define TACOZ_SET_UTF8_FLAG 0
#endif

#define ZIP_LFH_SIGNATURE 0x04034b50
#define ZIP_CDH_SIGNATURE 0x02014b50
#define ZIP_EOCD_SIGNATURE 0x06054b50
#define ZIP64_MARKER 0xFFFFFFFF
#define ZIP_VERSION_NEEDED_ZIP64 45
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
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
    p[4] = (unsigned char)(v >> 32);
    p[5] = (unsigned char)(v >> 40);
    p[6] = (unsigned char)(v >> 48);
    p[7] = (unsigned char)(v >> 56);
}

/* ----------------------- Timestamp conversion functions ------------------- */
static void unix_time_to_dos(time_t unix_time, uint16_t *dos_time, uint16_t *dos_date) {
    struct tm *tm_info = localtime(&unix_time);
    
    if (!tm_info) {
        *dos_time = 0;
        *dos_date = 0;
        return;
    }
    
    *dos_time = ((tm_info->tm_hour & 0x1F) << 11) |
                ((tm_info->tm_min & 0x3F) << 5) |
                ((tm_info->tm_sec / 2) & 0x1F);
    
    int year = tm_info->tm_year + 1900;
    if (year < 1980) year = 1980;
    
    *dos_date = (((year - 1980) & 0x7F) << 9) |
                (((tm_info->tm_mon + 1) & 0x0F) << 5) |
                (tm_info->tm_mday & 0x1F);
}

/* ----------------------- Metadata helper functions ------------------------- */
static void create_header_payload(const taco_meta_array_t *meta, unsigned char *payload) {
    memset(payload, 0, TACO_HEADER_PAYLOAD_SIZE);
    payload[0] = meta->count;
    payload[1] = payload[2] = payload[3] = 0;

    unsigned char *pairs_start = payload + 4;
    for (size_t i = 0; i < TACO_HEADER_MAX_ENTRIES; i++) {
        le64(pairs_start + i * 16 + 0, meta->entries[i].offset);
        le64(pairs_start + i * 16 + 8, meta->entries[i].length);
    }
}

static void parse_header_payload(const unsigned char *payload, taco_meta_array_t *meta) {
    meta->count = payload[0];
    if (meta->count > TACO_HEADER_MAX_ENTRIES) {
        meta->count = TACO_HEADER_MAX_ENTRIES;
    }

    const unsigned char *pairs_start = payload + 4;
    for (size_t i = 0; i < TACO_HEADER_MAX_ENTRIES; i++) {
        uint64_t offset = 0, length = 0;
        for (int j = 0; j < 8; j++) {
            offset |= ((uint64_t)pairs_start[i * 16 + j]) << (j * 8);
            length |= ((uint64_t)pairs_start[i * 16 + 8 + j]) << (j * 8);
        }
        meta->entries[i].offset = offset;
        meta->entries[i].length = length;
    }
}

typedef struct {
    uint32_t cd_entry_offset;
    uint64_t local_offset;
    uint16_t filename_len;
    char *filename;
    int matches_target;
} cd_entry_info_t;

static void cleanup_cd_entries(cd_entry_info_t *entries, uint16_t count) {
    if (!entries) return;
    for (uint16_t i = 0; i < count; i++) {
        free(entries[i].filename);
    }
    free(entries);
}

/* ----------------------- libzip helper functions --------------------------- */
static int is_directory(const char *src_path) {
    struct stat st;
    if (stat(src_path, &st) != 0) return -1;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int add_file_to_archive(zip_t *za, const char *src_path, const char *arc_name) {
    int path_type = is_directory(src_path);
    
    if (path_type == 1) {
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
        
        zip_source_t *source = zip_source_buffer(za, "", 0, 0);
        if (!source) {
            free(dir_name);
            return TACOZ_ERR_LIBZIP;
        }
        
        zip_int64_t index = zip_file_add(za, dir_name, source, ZIP_FL_OVERWRITE);
        if (index < 0) {
            zip_source_free(source);
            free(dir_name);
            return TACOZ_ERR_LIBZIP;
        }
        
        zip_uint32_t external_attr = (0755 | S_IFDIR) << 16;
        zip_file_set_external_attributes(za, (zip_uint64_t)index, ZIP_FL_UNCHANGED, ZIP_OPSYS_UNIX, external_attr);
        zip_set_file_compression(za, (zip_uint64_t)index, ZIP_CM_STORE, 0);
        
        free(dir_name);
        return TACOZ_OK;
        
    } else if (path_type == 0) {
        zip_source_t *source = zip_source_file(za, src_path, 0, -1);
        if (!source) return TACOZ_ERR_IO;

        zip_int64_t index = zip_file_add(za, arc_name, source, ZIP_FL_OVERWRITE);
        if (index < 0) {
            zip_source_free(source);
            return TACOZ_ERR_LIBZIP;
        }

        zip_set_file_compression(za, (zip_uint64_t)index, ZIP_CM_STORE, 0);
        return TACOZ_OK;
    }
    
    return TACOZ_ERR_IO;
}

static int add_header_to_archive(zip_t *za, const taco_meta_array_t *meta) {
    unsigned char *payload = malloc(TACO_HEADER_PAYLOAD_SIZE);
    if (!payload) return TACOZ_ERR_IO;
    
    create_header_payload(meta, payload);

    zip_source_t *source = zip_source_buffer(za, payload, TACO_HEADER_PAYLOAD_SIZE, 1);
    if (!source) {
        free(payload);
        return TACOZ_ERR_LIBZIP;
    }

    zip_int64_t index = zip_file_add(za, TACO_HEADER_NAME, source, ZIP_FL_OVERWRITE);
    if (index < 0) {
        zip_source_free(source);
        return TACOZ_ERR_LIBZIP;
    }

    zip_uint32_t external_attr = 0644 << 16;
    zip_file_set_external_attributes(za, (zip_uint64_t)index, ZIP_FL_UNCHANGED, ZIP_OPSYS_UNIX, external_attr);
    zip_set_file_compression(za, (zip_uint64_t)index, ZIP_CM_STORE, 0);

    return TACOZ_OK;
}

/* ----------------------- Direct ZIP manipulation functions ----------------- */
static int read_existing_cd_blob(FILE *fp, uint64_t *cd_offset, unsigned char **cd_data, 
                                uint32_t *cd_size, uint16_t *total_entries) {
    if (fseeko(fp, 0, SEEK_END) != 0) return TACOZ_ERR_IO;
    off_t file_size = ftello(fp);
    if (file_size < EOCD_MIN_SIZE) return TACOZ_ERR_INVALID_HEADER;
    
    size_t search_buffer_size = (file_size > LARGE_FILE_THRESHOLD) ? LARGE_SEARCH_BUFFER : SMALL_SEARCH_BUFFER;
    unsigned char *buffer = malloc(search_buffer_size);
    if (!buffer) return TACOZ_ERR_IO;
    
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
    
    for (long i = read_size - EOCD_MIN_SIZE; i >= 0; i--) {
        if (buffer[i] == 0x50 && buffer[i+1] == 0x4b && 
            buffer[i+2] == 0x05 && buffer[i+3] == 0x06) {
            
            unsigned char *eocd = buffer + i;
            *total_entries = eocd[10] | (eocd[11] << 8);
            *cd_size = eocd[12] | (eocd[13] << 8) | (eocd[14] << 16) | (eocd[15] << 24);
            
            uint32_t cd_offset_32 = eocd[16] | (eocd[17] << 8) | (eocd[18] << 16) | (eocd[19] << 24);
            *cd_offset = (cd_offset_32 == ZIP64_MARKER) ? (uint64_t)cd_offset_32 : (uint64_t)cd_offset_32;
            
            *cd_data = malloc(*cd_size);
            if (!*cd_data) {
                free(buffer);
                return TACOZ_ERR_IO;
            }
            
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

static int filename_exists_in_cd(const unsigned char *cd_data, uint32_t cd_size, const char *filename) {
    uint32_t offset = 0;
    
    while (offset < cd_size) {
        if (offset + 46 > cd_size) break;
        
        if (cd_data[offset] != 0x50 || cd_data[offset+1] != 0x4b ||
            cd_data[offset+2] != 0x01 || cd_data[offset+3] != 0x02) {
            break;
        }
        
        uint16_t filename_len = cd_data[offset+28] | (cd_data[offset+29] << 8);
        uint16_t extra_len = cd_data[offset+30] | (cd_data[offset+31] << 8);
        uint16_t comment_len = cd_data[offset+32] | (cd_data[offset+33] << 8);
        
        if (offset + 46 + filename_len > cd_size) break;
        
        if (filename_len == strlen(filename) && 
            memcmp(cd_data + offset + 46, filename, filename_len) == 0) {
            return 1;
        }
        
        offset += 46 + filename_len + extra_len + comment_len;
    }
    
    return 0;
}

static int write_local_file_header(FILE *fp, const char *filename, uint64_t file_size, 
                                  uint32_t crc32, time_t mtime) {
    unsigned char header[30];
    uint16_t filename_len = (uint16_t)strlen(filename);
    
    uint16_t dos_time, dos_date;
    unix_time_to_dos(mtime, &dos_time, &dos_date);
    
    le32(header + 0, ZIP_LFH_SIGNATURE);
    le16(header + 4, ZIP_VERSION_NEEDED_ZIP64);
    le16(header + 6, TACOZ_SET_UTF8_FLAG ? (1 << 11) : 0);
    le16(header + 8, 0);
    le16(header + 10, dos_time);
    le16(header + 12, dos_date);
    le32(header + 14, crc32);
    
    if (file_size >= ZIP64_MARKER) {
        le32(header + 18, ZIP64_MARKER);
        le32(header + 22, ZIP64_MARKER);
    } else {
        le32(header + 18, (uint32_t)file_size);
        le32(header + 22, (uint32_t)file_size);
    }
    
    le16(header + 26, filename_len);
    le16(header + 28, 0);
    
    if (fwrite(header, 1, 30, fp) != 30) return TACOZ_ERR_IO;
    if (fwrite(filename, 1, filename_len, fp) != filename_len) return TACOZ_ERR_IO;
    
    return TACOZ_OK;
}

static int copy_file_with_crc(FILE *src_fp, FILE *dest_fp, uint32_t *crc32_out, uint64_t *size_out) {
    unsigned char buffer[TACOZ_COPY_BUFSZ];
    uLong crc = crc32(0L, Z_NULL, 0);
    uint64_t total_size = 0;
    
    while (!feof(src_fp)) {
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), src_fp);
        if (bytes_read == 0) break;
        
        crc = crc32(crc, buffer, bytes_read);
        
        if (fwrite(buffer, 1, bytes_read, dest_fp) != bytes_read) {
            return TACOZ_ERR_IO;
        }
        
        total_size += bytes_read;
    }
    
    *crc32_out = (uint32_t)crc;
    *size_out = total_size;
    return TACOZ_OK;
}

static int write_cd_entry(FILE *fp, const char *filename, uint64_t local_offset, 
                         uint64_t file_size, uint32_t crc32, time_t mtime, mode_t file_mode) {
    unsigned char header[46];
    uint16_t filename_len = (uint16_t)strlen(filename);
    
    uint16_t dos_time, dos_date;
    unix_time_to_dos(mtime, &dos_time, &dos_date);
    
    le32(header + 0, ZIP_CDH_SIGNATURE);
    le16(header + 4, (ZIP_OPSYS_UNIX << 8) | ZIP_VERSION_NEEDED_ZIP64);
    le16(header + 6, ZIP_VERSION_NEEDED_ZIP64);
    le16(header + 8, TACOZ_SET_UTF8_FLAG ? (1 << 11) : 0);
    le16(header + 10, 0);
    le16(header + 12, dos_time);
    le16(header + 14, dos_date);
    le32(header + 16, crc32);
    
    if (file_size >= ZIP64_MARKER) {
        le32(header + 20, ZIP64_MARKER);
        le32(header + 24, ZIP64_MARKER);
    } else {
        le32(header + 20, (uint32_t)file_size);
        le32(header + 24, (uint32_t)file_size);
    }
    
    le16(header + 28, filename_len);
    le16(header + 30, 0);
    le16(header + 32, 0);
    le16(header + 34, 0);
    le16(header + 36, 0);
    le32(header + 38, (file_mode & 0xFFFF) << 16);
    
    if (local_offset >= ZIP64_MARKER) {
        le32(header + 42, ZIP64_MARKER);
    } else {
        le32(header + 42, (uint32_t)local_offset);
    }
    
    if (fwrite(header, 1, 46, fp) != 46) return TACOZ_ERR_IO;
    if (fwrite(filename, 1, filename_len, fp) != filename_len) return TACOZ_ERR_IO;
    
    return TACOZ_OK;
}

static int write_eocd(FILE *fp, uint16_t total_entries, uint32_t cd_size, uint64_t cd_offset) {
    unsigned char eocd[22];
    
    le32(eocd + 0, ZIP_EOCD_SIGNATURE);
    le16(eocd + 4, 0);
    le16(eocd + 6, 0);
    le16(eocd + 8, total_entries);
    le16(eocd + 10, total_entries);
    le32(eocd + 12, cd_size);
    
    if (cd_offset >= ZIP64_MARKER) {
        le32(eocd + 16, ZIP64_MARKER);
    } else {
        le32(eocd + 16, (uint32_t)cd_offset);
    }
    
    le16(eocd + 20, 0);
    
    if (fwrite(eocd, 1, 22, fp) != 22) return TACOZ_ERR_IO;
    
    return TACOZ_OK;
}

static int calculate_header_payload_offset(FILE *fp, uint64_t *payload_offset) {
    unsigned char header[30];
    
    if (fseek(fp, 0, SEEK_SET) != 0) return TACOZ_ERR_IO;
    if (fread(header, 1, 30, fp) != 30) return TACOZ_ERR_IO;
    
    if (header[0] != 0x50 || header[1] != 0x4b || 
        header[2] != 0x03 || header[3] != 0x04) {
        return TACOZ_ERR_INVALID_HEADER;
    }
    
    uint16_t filename_len = header[26] | (header[27] << 8);
    uint16_t extra_len = header[28] | (header[29] << 8);
    
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
    
    *payload_offset = 30 + filename_len + extra_len;
    
    return TACOZ_OK;
}

static int update_header_cd_crc32(FILE *fp, uint32_t new_crc32) {
    uint64_t cd_offset;
    unsigned char *cd_data;
    uint32_t cd_size;
    uint16_t total_entries;
    
    int rc = read_existing_cd_blob(fp, &cd_offset, &cd_data, &cd_size, &total_entries);
    if (rc != TACOZ_OK) return rc;
    
    uint32_t offset = 0;
    int found = 0;
    uint32_t header_cd_offset = 0;
    
    while (offset < cd_size && !found) {
        if (offset + 46 > cd_size) break;
        
        if (cd_data[offset] != 0x50 || cd_data[offset+1] != 0x4b ||
            cd_data[offset+2] != 0x01 || cd_data[offset+3] != 0x02) {
            break;
        }
        
        uint16_t filename_len = cd_data[offset+28] | (cd_data[offset+29] << 8);
        uint16_t extra_len = cd_data[offset+30] | (cd_data[offset+31] << 8);
        uint16_t comment_len = cd_data[offset+32] | (cd_data[offset+33] << 8);
        
        if (offset + 46 + filename_len > cd_size) break;
        
        if (filename_len == TACO_HEADER_NAME_LEN && 
            memcmp(cd_data + offset + 46, TACO_HEADER_NAME, TACO_HEADER_NAME_LEN) == 0) {
            header_cd_offset = offset;
            found = 1;
            break;
        }
        
        offset += 46 + filename_len + extra_len + comment_len;
    }
    
    if (!found) {
        free(cd_data);
        return TACOZ_ERR_NOT_FOUND;
    }
    
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

static int write_header_payload_direct(FILE *fp, uint64_t offset, const taco_meta_array_t *meta) {
    unsigned char payload[TACO_HEADER_PAYLOAD_SIZE];
    create_header_payload(meta, payload);
    
    if (fseek(fp, offset, SEEK_SET) != 0) return TACOZ_ERR_IO;
    if (fwrite(payload, 1, TACO_HEADER_PAYLOAD_SIZE, fp) != TACO_HEADER_PAYLOAD_SIZE) return TACOZ_ERR_IO;
    
    uLong new_crc = crc32(0L, Z_NULL, 0);
    new_crc = crc32(new_crc, payload, TACO_HEADER_PAYLOAD_SIZE);
    uint32_t final_crc = (uint32_t)new_crc;
    
    if (fseek(fp, 14, SEEK_SET) != 0) return TACOZ_ERR_IO;
    
    unsigned char crc_bytes[4];
    le32(crc_bytes, final_crc);
    if (fwrite(crc_bytes, 1, 4, fp) != 4) return TACOZ_ERR_IO;
    
    int rc = update_header_cd_crc32(fp, final_crc);
    if (rc != TACOZ_OK) return rc;
    
    if (fflush(fp) != 0) return TACOZ_ERR_IO;
    
    return TACOZ_OK;
}

static int read_header_payload_direct(FILE *fp, uint64_t offset, taco_meta_array_t *meta) {
    if (fseek(fp, offset, SEEK_SET) != 0) return TACOZ_ERR_IO;
    
    unsigned char payload[TACO_HEADER_PAYLOAD_SIZE];
    if (fread(payload, 1, TACO_HEADER_PAYLOAD_SIZE, fp) != TACO_HEADER_PAYLOAD_SIZE) {
        return TACOZ_ERR_IO;
    }
    
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
    if (!zip_path || !src_files || !arc_files || !meta || num_files == 0)
        return TACOZ_ERR_PARAM;

    int error;
    zip_t *za = zip_open(zip_path, ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!za) return TACOZ_ERR_IO;

    TACOZIP_DEBUG("Creating archive '%s' with %zu files", zip_path, num_files);

    int rc = add_header_to_archive(za, meta);
    if (rc != TACOZ_OK) {
        zip_discard(za);
        return rc;
    }

    for (size_t i = 0; i < num_files; i++) {
        rc = add_file_to_archive(za, src_files[i], arc_files[i]);
        if (rc != TACOZ_OK) {
            zip_discard(za);
            return rc;
        }
    }

    if (zip_close(za) < 0) return TACOZ_ERR_IO;

    return TACOZ_OK;
}

int tacozip_update_header(const char *zip_path, const taco_meta_array_t *meta) {
    if (!zip_path || !meta) return TACOZ_ERR_PARAM;

    FILE *fp = fopen(zip_path, "r+b");
    if (!fp) return TACOZ_ERR_IO;

    TACOZIP_DEBUG("Updating header metadata in '%s'", zip_path);

    uint64_t payload_offset;
    int rc = calculate_header_payload_offset(fp, &payload_offset);
    if (rc != TACOZ_OK) {
        fclose(fp);
        return rc;
    }

    rc = write_header_payload_direct(fp, payload_offset, meta);
    fclose(fp);
    
    return rc;
}

int tacozip_read_header(const char *zip_path, taco_meta_array_t *meta_out) {
    if (!zip_path || !meta_out) return TACOZ_ERR_PARAM;

    memset(meta_out, 0, sizeof(taco_meta_array_t));

    FILE *fp = fopen(zip_path, "rb");
    if (!fp) return TACOZ_ERR_IO;

    uint64_t payload_offset;
    int rc = calculate_header_payload_offset(fp, &payload_offset);
    if (rc != TACOZ_OK) {
        fclose(fp);
        return rc;
    }

    rc = read_header_payload_direct(fp, payload_offset, meta_out);
    fclose(fp);
    
    TACOZIP_DEBUG("Read header: %u entries", meta_out->count);
    return rc;
}

int tacozip_append_files(const char *zip_path,
                        const tacozip_append_entry_t *entries,
                        size_t num_entries) {
    if (!zip_path || !entries || num_entries == 0) return TACOZ_ERR_PARAM;

    TACOZIP_DEBUG("Appending %zu files to '%s'", num_entries, zip_path);

    FILE *fp = fopen(zip_path, "r+b");
    if (!fp) return TACOZ_ERR_IO;

    uint64_t old_cd_offset;
    unsigned char *existing_cd_data;
    uint32_t existing_cd_size;
    uint16_t existing_count;
    int rc = read_existing_cd_blob(fp, &old_cd_offset, &existing_cd_data, &existing_cd_size, &existing_count);
    if (rc != TACOZ_OK) {
        fclose(fp);
        return rc;
    }

    for (size_t i = 0; i < num_entries; i++) {
        if (filename_exists_in_cd(existing_cd_data, existing_cd_size, entries[i].arc_name) == 1) {
            free(existing_cd_data);
            fclose(fp);
            return TACOZ_ERR_EXISTS;
        }
    }

    if (fseeko(fp, old_cd_offset, SEEK_SET) != 0) {
        free(existing_cd_data);
        fclose(fp);
        return TACOZ_ERR_IO;
    }

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

    for (size_t i = 0; i < num_entries; i++) {
        new_files[i].local_offset = ftello(fp);
        
        FILE *src_fp = fopen(entries[i].src_path, "rb");
        if (!src_fp) {
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return TACOZ_ERR_IO;
        }

        struct stat st;
        if (stat(entries[i].src_path, &st) != 0) {
            fclose(src_fp);
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return TACOZ_ERR_IO;
        }
        
        new_files[i].mtime = st.st_mtime;
        new_files[i].file_mode = st.st_mode;

        rc = write_local_file_header(fp, entries[i].arc_name, st.st_size, 0, st.st_mtime);
        if (rc != TACOZ_OK) {
            fclose(src_fp);
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return rc;
        }

        rc = copy_file_with_crc(src_fp, fp, &new_files[i].crc32, &new_files[i].file_size);
        fclose(src_fp);
        
        if (rc != TACOZ_OK) {
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return rc;
        }

        off_t current_pos = ftello(fp);
        off_t crc_offset = new_files[i].local_offset + 14;
        
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
        
        if (fseeko(fp, current_pos, SEEK_SET) != 0) {
            free(new_files);
            free(existing_cd_data);
            fclose(fp);
            return TACOZ_ERR_IO;
        }
    }

    uint64_t new_cd_offset = ftello(fp);
    
    if (fwrite(existing_cd_data, 1, existing_cd_size, fp) != existing_cd_size) {
        free(new_files);
        free(existing_cd_data);
        fclose(fp);
        return TACOZ_ERR_IO;
    }
    
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

    uint16_t total_entries = existing_count + num_entries;
    uint32_t total_cd_size = existing_cd_size + new_entries_size;
    rc = write_eocd(fp, total_entries, total_cd_size, new_cd_offset);
    if (rc != TACOZ_OK) {
        free(new_files);
        free(existing_cd_data);
        fclose(fp);
        return rc;
    }

    if (ftruncate(fileno(fp), ftello(fp)) != 0) {
        free(new_files);
        free(existing_cd_data);
        fclose(fp);
        return TACOZ_ERR_IO;
    }

    free(new_files);
    free(existing_cd_data);
    fclose(fp);
    
    return TACOZ_OK;
}

int tacozip_replace_file(const char *zip_path,
                        const char *file_name,
                        const char *new_src_path) {
    if (!zip_path || !file_name || !new_src_path) return TACOZ_ERR_PARAM;

    int error;
    zip_t *za = zip_open(zip_path, 0, &error);
    if (!za) return TACOZ_ERR_IO;

    zip_int64_t file_index = zip_name_locate(za, file_name, 0);
    if (file_index < 0) {
        zip_close(za);
        return TACOZ_ERR_NOT_FOUND;
    }

    if (strcmp(file_name, TACO_HEADER_NAME) == 0) {
        zip_close(za);
        return TACOZ_ERR_PARAM;
    }

    zip_source_t *source = zip_source_file(za, new_src_path, 0, -1);
    if (!source) {
        zip_close(za);
        return TACOZ_ERR_IO;
    }

    if (zip_file_replace(za, (zip_uint64_t)file_index, source, 0) < 0) {
        zip_source_free(source);
        zip_close(za);
        return TACOZ_ERR_LIBZIP;
    }

    zip_set_file_compression(za, (zip_uint64_t)file_index, ZIP_CM_STORE, 0);

    if (zip_close(za) < 0) return TACOZ_ERR_IO;

    return TACOZ_OK;
}

int tacozip_trim_from(const char *zip_path, const char *target) {
    if (!zip_path || !target) return TACOZ_ERR_PARAM;
    
    int is_metadata = (strcmp(target, "METADATA/") == 0);
    int is_collection = (strcmp(target, "COLLECTION.json") == 0);
    
    if (!is_metadata && !is_collection) return TACOZ_ERR_PARAM;

    TACOZIP_DEBUG("Trimming '%s' from target '%s'", zip_path, target);
    
    FILE *fp = fopen(zip_path, "r+b");
    if (!fp) return TACOZ_ERR_IO;
    
    uint64_t cd_offset;
    unsigned char *cd_data = NULL;
    uint32_t cd_size;
    uint16_t total_entries;
    
    int rc = read_existing_cd_blob(fp, &cd_offset, &cd_data, &cd_size, &total_entries);
    if (rc != TACOZ_OK) {
        fclose(fp);
        return rc;
    }
    
    cd_entry_info_t *entries = malloc(total_entries * sizeof(cd_entry_info_t));
    memset(entries, 0, total_entries * sizeof(cd_entry_info_t));
    
    uint32_t cd_offset_iter = 0;
    uint16_t parsed_entries = 0;
    uint64_t trim_start_offset = UINT64_MAX;
    uint16_t matching_entries = 0;
    
    while (cd_offset_iter < cd_size && parsed_entries < total_entries) {
        if (cd_offset_iter + 46 > cd_size) break;
        
        if (cd_data[cd_offset_iter] != 0x50 || cd_data[cd_offset_iter+1] != 0x4b ||
            cd_data[cd_offset_iter+2] != 0x01 || cd_data[cd_offset_iter+3] != 0x02) {
            break;
        }
        
        uint16_t filename_len = cd_data[cd_offset_iter+28] | (cd_data[cd_offset_iter+29] << 8);
        uint16_t extra_len = cd_data[cd_offset_iter+30] | (cd_data[cd_offset_iter+31] << 8);
        uint16_t comment_len = cd_data[cd_offset_iter+32] | (cd_data[cd_offset_iter+33] << 8);
        
        if (cd_offset_iter + 46 + filename_len > cd_size) break;
        
        uint32_t local_offset_32 = cd_data[cd_offset_iter+42] | (cd_data[cd_offset_iter+43] << 8) | 
                                   (cd_data[cd_offset_iter+44] << 16) | (cd_data[cd_offset_iter+45] << 24);
        
        uint64_t local_offset;
        if (local_offset_32 == ZIP64_MARKER) {
            rc = TACOZ_ERR_INVALID_HEADER;
            goto cleanup;
        } else {
            local_offset = local_offset_32;
        }
        
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
        
        int matches = 0;
        if (is_metadata) {
            matches = (strncmp(entries[parsed_entries].filename, "METADATA/", 9) == 0);
        } else if (is_collection) {
            matches = (strcmp(entries[parsed_entries].filename, "COLLECTION.json") == 0);
        }
        
        entries[parsed_entries].matches_target = matches;
        
        if (matches) {
            matching_entries++;
            if (local_offset < trim_start_offset) {
                trim_start_offset = local_offset;
            }
        }
        
        parsed_entries++;
        cd_offset_iter += 46 + filename_len + extra_len + comment_len;
    }
    
    if (matching_entries == 0) {
        rc = TACOZ_ERR_NOT_FOUND;
        goto cleanup;
    }
    
    if (trim_start_offset == UINT64_MAX) {
        rc = TACOZ_ERR_INVALID_HEADER;
        goto cleanup;
    }
    
    for (uint16_t i = 0; i < parsed_entries; i++) {
        if (!entries[i].matches_target && entries[i].local_offset >= trim_start_offset) {
            rc = TACOZ_ERR_PARAM;
            goto cleanup;
        }
    }
    
    if (ftruncate(fileno(fp), trim_start_offset) != 0) {
        rc = TACOZ_ERR_IO;
        goto cleanup;
    }
    
    if (fseeko(fp, trim_start_offset, SEEK_SET) != 0) {
        rc = TACOZ_ERR_IO;
        goto cleanup;
    }
    
    uint64_t new_cd_offset = trim_start_offset;
    uint16_t remaining_entries = 0;
    uint32_t new_cd_size = 0;
    
    for (uint16_t i = 0; i < parsed_entries; i++) {
        if (!entries[i].matches_target) {
            uint32_t entry_start = entries[i].cd_entry_offset;
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
        }
    }
    
    rc = write_eocd(fp, remaining_entries, new_cd_size, new_cd_offset);
    if (rc != TACOZ_OK) goto cleanup;
    
    rc = TACOZ_OK;

cleanup:
    cleanup_cd_entries(entries, parsed_entries);
    free(cd_data);
    if (fp) fclose(fp);
    return rc;
}