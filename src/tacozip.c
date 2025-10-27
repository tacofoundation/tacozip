/*
 * tacozip.c — ZIP (STORE-only) writer with libzip backend and TACO Header supporting up to 7 metadata entries.
 *
 * This simplified implementation provides a clean API with only the essential functions:
 *
 * - tacozip_create() - create archive with up to 7 metadata entries
 * - tacozip_update_header() - write/update header metadata (optimized, bypasses libzip)
 * - tacozip_read_header() - read header metadata (optimized, bypasses libzip)
 * - tacozip_get_version() - get library version
 * - tacozip_detect_format() - detect ZIP32/ZIP64 format 
 *
 */

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
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define fileno _fileno
#else
#include <unistd.h>
#endif

#include <zip.h>
#include <zlib.h>

#ifndef TACOZIP_VERSION_STRING
#define TACOZIP_VERSION_STRING "0.0.0"
#endif

/* ========================================================================== */
/*                            DEBUG UTILITIES                                */
/* ========================================================================== */

static int tacozip_debug_enabled = -1;

static inline int tacozip_should_debug(void) {
  if (tacozip_debug_enabled == -1) {
    const char *env = getenv("TACOZIP_DEBUG");
    tacozip_debug_enabled =
        (env && (strcmp(env, "1") == 0 || strcmp(env, "ON") == 0 ||
                 strcmp(env, "TRUE") == 0))
            ? 1
            : 0;
  }
  return tacozip_debug_enabled;
}

#define TACOZIP_DEBUG(...)                                                     \
  do {                                                                         \
    if (tacozip_should_debug()) {                                              \
      fprintf(stderr, "[TACOZIP] " __VA_ARGS__);                               \
      fprintf(stderr, "\n");                                                   \
    }                                                                          \
  } while (0)

/* ========================================================================== */
/*                              CONSTANTS                                    */
/* ========================================================================== */

#define ZIP_LFH_SIGNATURE 0x04034b50
#define ZIP_CDH_SIGNATURE 0x02014b50
#define ZIP_EOCD_SIGNATURE 0x06054b50
#define ZIP64_EOCD_SIGNATURE 0x06064b50
#define ZIP64_EOCD_LOCATOR_SIGNATURE 0x07064b50
#define ZIP_VERSION_NEEDED 20
#define EOCD_MIN_SIZE 22
#define ZIP64_EOCD_MIN_SIZE 56
#define ZIP64_EOCD_LOCATOR_SIZE 20

/* ZIP specification allows up to 65535 bytes for file comment after EOCD */
#define MAX_ZIP_COMMENT_LENGTH 65535
#define MAX_EOCD_SEARCH (MAX_ZIP_COMMENT_LENGTH + ZIP64_EOCD_MIN_SIZE)

#ifndef TACOZ_SET_UTF8_FLAG
#define TACOZ_SET_UTF8_FLAG 0
#endif

/* ========================================================================== */
/*                         BYTE ORDER UTILITIES                              */
/* ========================================================================== */

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

static inline void le64(unsigned char *p, uint64_t v) {
  p[0] = (unsigned char)(v);
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16);
  p[3] = (unsigned char)(v >> 24);
  p[4] = (unsigned char)(v >> 32);
  p[5] = (unsigned char)(v >> 40);
  p[6] = (unsigned char)(v >> 48);
  p[7] = (unsigned char)(v >> 56);
}

static inline uint16_t read_le16(const unsigned char *p) {
  return p[0] | (p[1] << 8);
}

static inline uint32_t read_le32(const unsigned char *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static inline uint64_t read_le64(const unsigned char *p) {
  return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4) << 32);
}

/* ========================================================================== */
/*                          DOS TIME CONVERSION                              */
/* ========================================================================== */

static void unix_time_to_dos(time_t unix_time, uint16_t *dos_time,
                             uint16_t *dos_date) {
  struct tm tm_info;
  struct tm *tm_ptr = NULL;

#ifdef _WIN32
  if (localtime_s(&tm_info, &unix_time) != 0) {
    *dos_time = 0;
    *dos_date = 0;
    return;
  }
  tm_ptr = &tm_info;
#else
  tm_ptr = localtime_r(&unix_time, &tm_info);
  if (!tm_ptr) {
    *dos_time = 0;
    *dos_date = 0;
    return;
  }
#endif

  *dos_time = ((tm_ptr->tm_hour & 0x1F) << 11) |
              ((tm_ptr->tm_min & 0x3F) << 5) | 
              ((tm_ptr->tm_sec / 2) & 0x1F);

  int year = tm_ptr->tm_year + 1900;
  if (year < 1980)
    year = 1980;

  *dos_date = (((year - 1980) & 0x7F) << 9) |
              (((tm_ptr->tm_mon + 1) & 0x0F) << 5) | 
              (tm_ptr->tm_mday & 0x1F);
}

/* ========================================================================== */
/*                        TACO HEADER UTILITIES                              */
/* ========================================================================== */

static inline void create_header_payload(const taco_meta_array_t *meta,
                                         unsigned char *payload) {
  memset(payload, 0, TACO_HEADER_PAYLOAD_SIZE);
  payload[0] = meta->count;
  
  unsigned char *pairs = payload + 4;
  for (size_t i = 0; i < TACO_HEADER_MAX_ENTRIES; i++) {
    le64(pairs, meta->entries[i].offset);
    le64(pairs + 8, meta->entries[i].length);
    pairs += 16;
  }
}

static inline int parse_header_payload(const unsigned char *payload,
                                       taco_meta_array_t *meta) {
  uint8_t count = payload[0];
  if (count > TACO_HEADER_MAX_ENTRIES) {
    return TACOZ_ERR_INVALID_HEADER;
  }
  
  meta->count = count;
  const unsigned char *pairs = payload + 4;
  
  for (size_t i = 0; i < TACO_HEADER_MAX_ENTRIES; i++) {
    meta->entries[i].offset = read_le64(pairs);
    meta->entries[i].length = read_le64(pairs + 8);
    pairs += 16;
  }
  
  return TACOZ_OK;
}

/* ========================================================================== */
/*                      FORMAT DETECTION IMPLEMENTATION                      */
/* ========================================================================== */

int tacozip_detect_format(const char *zip_path) {
  if (!zip_path)
    return TACOZIP_FORMAT_UNKNOWN;

  FILE *fp = fopen(zip_path, "rb");
  if (!fp)
    return TACOZIP_FORMAT_UNKNOWN;

  if (fseeko(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return TACOZIP_FORMAT_UNKNOWN;
  }

  off_t file_size = ftello(fp);
  if (file_size < EOCD_MIN_SIZE) {
    fclose(fp);
    return TACOZIP_FORMAT_UNKNOWN;
  }

  // Search from end of file, accounting for possible ZIP comment
  size_t search_size = (file_size > MAX_EOCD_SEARCH) 
                       ? MAX_EOCD_SEARCH 
                       : (size_t)file_size;

  unsigned char *buffer = malloc(search_size);
  if (!buffer) {
    fclose(fp);
    return TACOZIP_FORMAT_UNKNOWN;
  }

  off_t search_start = file_size - search_size;
  if (fseeko(fp, search_start, SEEK_SET) != 0) {
    free(buffer);
    fclose(fp);
    return TACOZIP_FORMAT_UNKNOWN;
  }

  size_t bytes_read = fread(buffer, 1, search_size, fp);
  fclose(fp);

  if (bytes_read != search_size) {
    free(buffer);
    return TACOZIP_FORMAT_UNKNOWN;
  }

  int result = TACOZIP_FORMAT_UNKNOWN;
  
  // First search for ZIP64 EOCD Locator (most reliable way to detect ZIP64)
  for (long i = bytes_read - ZIP64_EOCD_LOCATOR_SIZE; i >= 0; i--) {
    if (read_le32(buffer + i) == ZIP64_EOCD_LOCATOR_SIGNATURE) {
      TACOZIP_DEBUG("Detected ZIP64 format (found EOCD Locator)");
      result = TACOZIP_FORMAT_ZIP64;
      break;
    }
  }
  
  // If no ZIP64 locator, search for regular EOCD
  if (result == TACOZIP_FORMAT_UNKNOWN) {
    for (long i = bytes_read - EOCD_MIN_SIZE; i >= 0; i--) {
      if (read_le32(buffer + i) == ZIP_EOCD_SIGNATURE) {
        // Check if CD offset/size have ZIP64 markers
        uint32_t cd_size = read_le32(buffer + i + 12);
        uint32_t cd_offset = read_le32(buffer + i + 16);
        
        if (cd_size == 0xFFFFFFFF || cd_offset == 0xFFFFFFFF) {
          TACOZIP_DEBUG("Detected ZIP64 format (found markers in EOCD)");
          result = TACOZIP_FORMAT_ZIP64;
        } else {
          TACOZIP_DEBUG("Detected ZIP32 format");
          result = TACOZIP_FORMAT_ZIP32;
        }
        break;
      }
    }
  }

  free(buffer);
  return result;
}

/* ========================================================================== */
/*                      VALIDATION IMPLEMENTATION                            */
/* ========================================================================== */

const char *tacozip_validate_error_string(int result) {
  switch (result) {
    case TACOZ_VALID:
      return "Valid TACO archive";
    
    /* Level 1 errors */
    case TACOZ_INVALID_NOT_ZIP:
      return "Not a ZIP file (missing LFH signature)";
    case TACOZ_INVALID_NO_TACO:
      return "No TACO_HEADER at offset 0 (file modified by external tool)";
    case TACOZ_INVALID_HEADER_SIZE:
      return "Invalid header size (corrupted)";
    case TACOZ_INVALID_META_COUNT:
      return "Invalid metadata count (must be 0-7)";
    case TACOZ_INVALID_FILE_SIZE:
      return "File too small to be valid archive";
    
    /* Level 2 errors */
    case TACOZ_INVALID_NO_EOCD:
      return "No End of Central Directory record found";
    case TACOZ_INVALID_CD_OFFSET:
      return "Invalid Central Directory offset";
    case TACOZ_INVALID_NO_CD_ENTRY:
      return "TACO_HEADER not found in Central Directory";
    case TACOZ_INVALID_REORDERED:
      return "Archive entries reordered (CD doesn't point to offset 0)";
    
    /* Level 3 errors */
    case TACOZ_INVALID_CRC_LFH:
      return "CRC32 mismatch in Local File Header";
    case TACOZ_INVALID_CRC_CD:
      return "CRC32 mismatch in Central Directory";
    
    default:
      return "Unknown validation error";
  }
}

int tacozip_validate(const char *zip_path, tacozip_validate_level_t level) {
  if (!zip_path)
    return TACOZ_ERR_PARAM;

  TACOZIP_DEBUG("Validating '%s' with level %d", zip_path, level);

  /* ===================================================================== */
  /* LEVEL 1: QUICK - Header Checks                                       */
  /* ===================================================================== */

  FILE *fp = fopen(zip_path, "rb");
  if (!fp)
    return TACOZ_ERR_IO;

  /* Check 1: File size must be at least header + EOCD */
  if (fseeko(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return TACOZ_ERR_IO;
  }

  off_t file_size = ftello(fp);
  if (file_size < TACO_HEADER_TOTAL_SIZE + EOCD_MIN_SIZE) {
    fclose(fp);
    TACOZIP_DEBUG("File too small: %lld bytes", (long long)file_size);
    return TACOZ_INVALID_FILE_SIZE;
  }

  /* Check 2: Read first 157 bytes (TACO_HEADER) */
  if (fseeko(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return TACOZ_ERR_IO;
  }

  unsigned char header[TACO_HEADER_TOTAL_SIZE];
  if (fread(header, 1, TACO_HEADER_TOTAL_SIZE, fp) != TACO_HEADER_TOTAL_SIZE) {
    fclose(fp);
    return TACOZ_ERR_IO;
  }

  /* Check 3: Verify LFH signature */
  if (read_le32(header) != ZIP_LFH_SIGNATURE) {
    fclose(fp);
    TACOZIP_DEBUG("Not a ZIP file: invalid LFH signature");
    return TACOZ_INVALID_NOT_ZIP;
  }

  /* Check 4: Verify filename is "TACO_HEADER" */
  if (memcmp(header + 30, TACO_HEADER_NAME, TACO_HEADER_NAME_LEN) != 0) {
    fclose(fp);
    TACOZIP_DEBUG("TACO_HEADER not at offset 0");
    return TACOZ_INVALID_NO_TACO;
  }

  /* Check 5: Verify compressed and uncompressed size == 116 */
  uint32_t compressed_size = read_le32(header + 18);
  uint32_t uncompressed_size = read_le32(header + 22);
  if (compressed_size != TACO_HEADER_PAYLOAD_SIZE || 
      uncompressed_size != TACO_HEADER_PAYLOAD_SIZE) {
    fclose(fp);
    TACOZIP_DEBUG("Invalid header size: compressed=%u, uncompressed=%u", 
                  compressed_size, uncompressed_size);
    return TACOZ_INVALID_HEADER_SIZE;
  }

  /* Check 6: Parse and validate metadata count */
  taco_meta_array_t meta = {0};
  int rc = parse_header_payload(header + 41, &meta);
  if (rc != TACOZ_OK || meta.count > TACO_HEADER_MAX_ENTRIES) {
    fclose(fp);
    TACOZIP_DEBUG("Invalid metadata count: %u", meta.count);
    return TACOZ_INVALID_META_COUNT;
  }

  TACOZIP_DEBUG("Level 1 (QUICK) validation passed");

  if (level == TACOZIP_VALIDATE_QUICK) {
    fclose(fp);
    return TACOZ_VALID;
  }

  /* ===================================================================== */
  /* LEVEL 2: NORMAL - Structure Checks                                   */
  /* ===================================================================== */

  /* Check 7: Find EOCD and extract CD info */
  size_t search_size = (file_size > MAX_EOCD_SEARCH) 
                       ? MAX_EOCD_SEARCH 
                       : (size_t)file_size;

  unsigned char *buffer = malloc(search_size);
  if (!buffer) {
    fclose(fp);
    return TACOZ_ERR_IO;
  }

  off_t search_start = file_size - search_size;
  if (fseeko(fp, search_start, SEEK_SET) != 0) {
    free(buffer);
    fclose(fp);
    return TACOZ_ERR_IO;
  }

  size_t bytes_read = fread(buffer, 1, search_size, fp);
  if (bytes_read != search_size) {
    free(buffer);
    fclose(fp);
    return TACOZ_ERR_IO;
  }

  uint64_t cd_offset = 0;
  uint64_t cd_size = 0;
  int found_eocd = 0;

  /* Search for ZIP64 EOCD Locator first */
  for (long i = bytes_read - ZIP64_EOCD_LOCATOR_SIZE; i >= 0; i--) {
    if (read_le32(buffer + i) == ZIP64_EOCD_LOCATOR_SIGNATURE) {
      uint64_t zip64_eocd_offset = read_le64(buffer + i + 8);
      
      unsigned char zip64_eocd[ZIP64_EOCD_MIN_SIZE];
      if (fseeko(fp, (off_t)zip64_eocd_offset, SEEK_SET) == 0 &&
          fread(zip64_eocd, 1, ZIP64_EOCD_MIN_SIZE, fp) == ZIP64_EOCD_MIN_SIZE &&
          read_le32(zip64_eocd) == ZIP64_EOCD_SIGNATURE) {
        cd_size = read_le64(zip64_eocd + 40);
        cd_offset = read_le64(zip64_eocd + 48);
        found_eocd = 1;
        TACOZIP_DEBUG("Found ZIP64 EOCD: cd_offset=%llu", 
                      (unsigned long long)cd_offset);
        break;
      }
    }
  }

  /* If no ZIP64, search for regular EOCD */
  if (!found_eocd) {
    for (long i = bytes_read - EOCD_MIN_SIZE; i >= 0; i--) {
      if (read_le32(buffer + i) == ZIP_EOCD_SIGNATURE) {
        uint32_t cd_size32 = read_le32(buffer + i + 12);
        uint32_t cd_offset32 = read_le32(buffer + i + 16);
        
        if (cd_size32 != 0xFFFFFFFF && cd_offset32 != 0xFFFFFFFF) {
          cd_size = cd_size32;
          cd_offset = cd_offset32;
          found_eocd = 1;
          TACOZIP_DEBUG("Found ZIP32 EOCD: cd_offset=%u", cd_offset32);
          break;
        }
      }
    }
  }

  free(buffer);

  if (!found_eocd) {
    fclose(fp);
    TACOZIP_DEBUG("No EOCD found");
    return TACOZ_INVALID_NO_EOCD;
  }

  /* Check 8: Validate CD offset is reasonable */
  if (cd_offset >= (uint64_t)file_size || cd_size > (uint64_t)file_size) {
    fclose(fp);
    TACOZIP_DEBUG("Invalid CD: offset=%llu, size=%llu, file_size=%lld",
                  (unsigned long long)cd_offset, 
                  (unsigned long long)cd_size,
                  (long long)file_size);
    return TACOZ_INVALID_CD_OFFSET;
  }

  /* Check 9: Read CD and find TACO_HEADER entry */
  if (cd_size > 0xFFFFFFFF) {
    fclose(fp);
    return TACOZ_ERR_TOO_LARGE;
  }

  unsigned char *cd_data = malloc((size_t)cd_size);
  if (!cd_data) {
    fclose(fp);
    return TACOZ_ERR_IO;
  }

  if (fseeko(fp, (off_t)cd_offset, SEEK_SET) != 0) {
    free(cd_data);
    fclose(fp);
    return TACOZ_ERR_IO;
  }

  if (fread(cd_data, 1, (size_t)cd_size, fp) != (size_t)cd_size) {
    free(cd_data);
    fclose(fp);
    return TACOZ_ERR_IO;
  }

  /* Search for TACO_HEADER in CD */
  uint64_t offset = 0;
  int found_header = 0;
  uint64_t header_cd_offset = 0;
  uint32_t cd_entry_crc32 = 0;

  while (offset + 46 <= cd_size) {
    if (read_le32(cd_data + offset) != ZIP_CDH_SIGNATURE)
      break;

    uint16_t filename_len = read_le16(cd_data + offset + 28);
    uint16_t extra_len = read_le16(cd_data + offset + 30);
    uint16_t comment_len = read_le16(cd_data + offset + 32);

    if (offset + 46 + filename_len + extra_len + comment_len > cd_size)
      break;

    if (offset + 46 + filename_len <= cd_size &&
        filename_len == TACO_HEADER_NAME_LEN &&
        memcmp(cd_data + offset + 46, TACO_HEADER_NAME, TACO_HEADER_NAME_LEN) == 0) {
      header_cd_offset = offset;
      cd_entry_crc32 = read_le32(cd_data + offset + 16);
      found_header = 1;
      TACOZIP_DEBUG("Found TACO_HEADER in CD at offset %llu", 
                    (unsigned long long)header_cd_offset);
      break;
    }

    offset += 46 + filename_len + extra_len + comment_len;
  }

  if (!found_header) {
    free(cd_data);
    fclose(fp);
    TACOZIP_DEBUG("TACO_HEADER not found in CD");
    return TACOZ_INVALID_NO_CD_ENTRY;
  }

  /* Check 10: Verify CD entry points to offset 0 */
  uint32_t local_header_offset = read_le32(cd_data + header_cd_offset + 42);
  if (local_header_offset != 0) {
    free(cd_data);
    fclose(fp);
    TACOZIP_DEBUG("CD entry points to offset %u, not 0", local_header_offset);
    return TACOZ_INVALID_REORDERED;
  }

  free(cd_data);

  TACOZIP_DEBUG("Level 2 (NORMAL) validation passed");

  if (level == TACOZIP_VALIDATE_NORMAL) {
    fclose(fp);
    return TACOZ_VALID;
  }

  /* ===================================================================== */
  /* LEVEL 3: DEEP - CRC32 Validation                                     */
  /* ===================================================================== */

  /* Check 11: Calculate CRC32 of payload and compare with LFH */
  uint32_t lfh_crc32 = read_le32(header + 14);
  
  uLong calculated_crc = crc32(0L, Z_NULL, 0);
  calculated_crc = crc32(calculated_crc, header + 41, TACO_HEADER_PAYLOAD_SIZE);
  uint32_t expected_crc32 = (uint32_t)calculated_crc;

  if (lfh_crc32 != expected_crc32) {
    fclose(fp);
    TACOZIP_DEBUG("CRC32 mismatch in LFH: expected=0x%08x, found=0x%08x",
                  expected_crc32, lfh_crc32);
    return TACOZ_INVALID_CRC_LFH;
  }

  /* Check 12: Compare with CD entry CRC32 */
  if (cd_entry_crc32 != expected_crc32) {
    fclose(fp);
    TACOZIP_DEBUG("CRC32 mismatch in CD: expected=0x%08x, found=0x%08x",
                  expected_crc32, cd_entry_crc32);
    return TACOZ_INVALID_CRC_CD;
  }

  fclose(fp);

  TACOZIP_DEBUG("Level 3 (DEEP) validation passed");
  return TACOZ_VALID;
}

/* ========================================================================== */
/*                    CENTRAL DIRECTORY UTILITIES                            */
/* ========================================================================== */

static int find_cd_and_update_crc32(FILE *fp, uint32_t new_crc32) {
  if (fseeko(fp, 0, SEEK_END) != 0)
    return TACOZ_ERR_IO;
  
  off_t file_size = ftello(fp);
  if (file_size < EOCD_MIN_SIZE)
    return TACOZ_ERR_INVALID_HEADER;

  // Search from end of file, accounting for possible ZIP comment
  size_t search_size = (file_size > MAX_EOCD_SEARCH)
                       ? MAX_EOCD_SEARCH
                       : (size_t)file_size;

  unsigned char *buffer = malloc(search_size);
  if (!buffer)
    return TACOZ_ERR_IO;

  off_t search_start = file_size - search_size;
  if (fseeko(fp, search_start, SEEK_SET) != 0) {
    free(buffer);
    return TACOZ_ERR_IO;
  }

  size_t bytes_read = fread(buffer, 1, search_size, fp);
  if (bytes_read != search_size) {
    free(buffer);
    return TACOZ_ERR_IO;
  }

  uint64_t cd_offset = 0;
  uint64_t cd_size = 0;
  int found_cd = 0;
  int is_zip64 = 0;

  // Step 1: Search for ZIP64 EOCD Locator (0x07064b50)
  // This is the most robust way to detect ZIP64
  for (long i = bytes_read - ZIP64_EOCD_LOCATOR_SIZE; i >= 0; i--) {
    if (read_le32(buffer + i) == ZIP64_EOCD_LOCATOR_SIGNATURE) {
      TACOZIP_DEBUG("Found ZIP64 EOCD Locator at offset %lld",
                    (unsigned long long)search_start + i);
      
      // ZIP64 EOCD Locator structure:
      // Offset 0-3:   Signature (0x07064b50)
      // Offset 4-7:   Number of disk with ZIP64 EOCD
      // Offset 8-15:  Offset of ZIP64 EOCD record
      // Offset 16-19: Total number of disks
      
      uint64_t zip64_eocd_offset = read_le64(buffer + i + 8);
      
      TACOZIP_DEBUG("ZIP64 EOCD offset: %llu",
                    (unsigned long long)zip64_eocd_offset);
      
      // Step 2: Read the ZIP64 EOCD Record
      unsigned char zip64_eocd[ZIP64_EOCD_MIN_SIZE];
      if (fseeko(fp, (off_t)zip64_eocd_offset, SEEK_SET) != 0) {
        free(buffer);
        return TACOZ_ERR_IO;
      }
      
      if (fread(zip64_eocd, 1, ZIP64_EOCD_MIN_SIZE, fp) != ZIP64_EOCD_MIN_SIZE) {
        free(buffer);
        return TACOZ_ERR_IO;
      }
      
      // Verify ZIP64 EOCD signature
      if (read_le32(zip64_eocd) != ZIP64_EOCD_SIGNATURE) {
        TACOZIP_DEBUG("Invalid ZIP64 EOCD signature");
        free(buffer);
        return TACOZ_ERR_INVALID_HEADER;
      }
      
      // ZIP64 EOCD structure:
      // Offset 0-3:   Signature (0x06064b50)
      // Offset 4-11:  Size of ZIP64 EOCD record
      // Offset 12-13: Version made by
      // Offset 14-15: Version needed to extract
      // Offset 16-19: Number of this disk
      // Offset 20-23: Disk where CD starts
      // Offset 24-31: Number of CD entries on this disk
      // Offset 32-39: Total number of CD entries
      // Offset 40-47: Size of CD (64-bit)
      // Offset 48-55: Offset of CD (64-bit)
      
      cd_size = read_le64(zip64_eocd + 40);
      cd_offset = read_le64(zip64_eocd + 48);
      found_cd = 1;
      is_zip64 = 1;
      
      TACOZIP_DEBUG("ZIP64: cd_offset=%llu, cd_size=%llu", 
                    (unsigned long long)cd_offset, 
                    (unsigned long long)cd_size);
      break;
    }
  }

  // Step 3: If no ZIP64 locator found, search for regular EOCD
  if (!found_cd) {
    for (long i = bytes_read - EOCD_MIN_SIZE; i >= 0; i--) {
      if (read_le32(buffer + i) == ZIP_EOCD_SIGNATURE) {
        TACOZIP_DEBUG("Found ZIP32 EOCD at offset %lld", (long long)(search_start + i));
        
        // Regular EOCD structure:
        // Offset 0-3:   Signature (0x06054b50)
        // Offset 4-5:   Number of this disk
        // Offset 6-7:   Disk where CD starts
        // Offset 8-9:   Number of CD entries on this disk
        // Offset 10-11: Total number of CD entries
        // Offset 12-15: Size of CD (32-bit)
        // Offset 16-19: Offset of CD (32-bit)
        // Offset 20-21: Comment length
        
        uint32_t cd_size32 = read_le32(buffer + i + 12);
        uint32_t cd_offset32 = read_le32(buffer + i + 16);
        
        // Check for ZIP64 markers
        if (cd_size32 == 0xFFFFFFFF || cd_offset32 == 0xFFFFFFFF) {
          TACOZIP_DEBUG("Found ZIP64 markers but no ZIP64 EOCD Locator");
          free(buffer);
          return TACOZ_ERR_INVALID_HEADER;
        }
        
        cd_size = cd_size32;
        cd_offset = cd_offset32;
        found_cd = 1;
        
        TACOZIP_DEBUG("ZIP32: cd_offset=%u, cd_size=%u", 
                      cd_offset32, cd_size32);
        break;
      }
    }
  }

  free(buffer);

  if (!found_cd) {
    TACOZIP_DEBUG("No EOCD record found");
    return TACOZ_ERR_INVALID_HEADER;
  }

  // Step 4: Read Central Directory and find TACO_HEADER entry
  // Safety check: CD size must be reasonable (< 4GB for memory allocation)
  if (cd_size > 0xFFFFFFFF) {
    TACOZIP_DEBUG("CD size too large: %llu bytes", (unsigned long long)cd_size);
    return TACOZ_ERR_TOO_LARGE;
  }

  unsigned char *cd_data = malloc((size_t)cd_size);
  if (!cd_data)
    return TACOZ_ERR_IO;

  if (fseeko(fp, (off_t)cd_offset, SEEK_SET) != 0) {
    free(cd_data);
    return TACOZ_ERR_IO;
  }

  if (fread(cd_data, 1, (size_t)cd_size, fp) != (size_t)cd_size) {
    free(cd_data);
    return TACOZ_ERR_IO;
  }

  // Step 5: Find TACO_HEADER entry in CD with proper bounds checking
  uint64_t offset = 0;
  int found_header = 0;
  uint64_t header_cd_offset = 0;

  while (offset + 46 <= cd_size) {
    if (read_le32(cd_data + offset) != ZIP_CDH_SIGNATURE)
      break;

    uint16_t filename_len = read_le16(cd_data + offset + 28);
    uint16_t extra_len = read_le16(cd_data + offset + 30);
    uint16_t comment_len = read_le16(cd_data + offset + 32);

    // CRITICAL BOUNDS CHECK: Ensure we don't overflow
    if (offset + 46 + filename_len + extra_len + comment_len > cd_size) {
      TACOZIP_DEBUG("Corrupted CD: entry extends beyond CD size");
      break;
    }

    if (offset + 46 + filename_len <= cd_size &&
        filename_len == TACO_HEADER_NAME_LEN &&
        memcmp(cd_data + offset + 46, TACO_HEADER_NAME, TACO_HEADER_NAME_LEN) == 0) {
      header_cd_offset = offset;
      found_header = 1;
      TACOZIP_DEBUG("Found TACO_HEADER in CD at offset %llu", 
                    (unsigned long long)header_cd_offset);
      break;
    }

    offset += 46 + filename_len + extra_len + comment_len;
  }

  free(cd_data);

  if (!found_header) {
    TACOZIP_DEBUG("TACO_HEADER not found in CD");
    return TACOZ_ERR_NOT_FOUND;
  }

  // Step 6: Update CRC32 in CD entry
  // CRC32 is at offset 16 from the start of the CD entry
  uint64_t crc_file_offset = cd_offset + header_cd_offset + 16;
  
  if (fseeko(fp, (off_t)crc_file_offset, SEEK_SET) != 0)
    return TACOZ_ERR_IO;

  unsigned char crc_bytes[4];
  le32(crc_bytes, new_crc32);
  
  if (fwrite(crc_bytes, 1, 4, fp) != 4)
    return TACOZ_ERR_IO;

  TACOZIP_DEBUG("Updated CRC32 in CD to 0x%08x (format: %s)", 
                new_crc32, is_zip64 ? "ZIP64" : "ZIP32");
  
  return TACOZ_OK;
}

/* ========================================================================== */
/*                        LIBZIP HELPER FUNCTIONS                            */
/* ========================================================================== */

static inline int add_header_to_archive(zip_t *za, const taco_meta_array_t *meta) {
  unsigned char *payload = malloc(TACO_HEADER_PAYLOAD_SIZE);
  if (!payload)
    return TACOZ_ERR_IO;

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
  zip_file_set_external_attributes(za, (zip_uint64_t)index, ZIP_FL_UNCHANGED,
                                   ZIP_OPSYS_UNIX, external_attr);
  zip_set_file_compression(za, (zip_uint64_t)index, ZIP_CM_STORE, 0);

  return TACOZ_OK;
}

/* ========================================================================== */
/*                      PUBLIC API IMPLEMENTATION                            */
/* ========================================================================== */

int tacozip_parse_header(const unsigned char *buffer, size_t buffer_size,
                         taco_meta_array_t *meta_out) {
  if (!buffer || !meta_out || buffer_size < TACO_HEADER_TOTAL_SIZE) {
    return TACOZ_ERR_PARAM;
  }

  memset(meta_out, 0, sizeof(taco_meta_array_t));

  if (read_le32(buffer) != ZIP_LFH_SIGNATURE) {
    return TACOZ_ERR_INVALID_HEADER;
  }

  if (memcmp(buffer + 30, TACO_HEADER_NAME, TACO_HEADER_NAME_LEN) != 0) {
    return TACOZ_ERR_INVALID_HEADER;
  }

  return parse_header_payload(buffer + 41, meta_out);
}

int tacozip_serialize_header(const taco_meta_array_t *meta,
                             unsigned char *buffer, size_t buffer_size) {
  if (!meta || !buffer || buffer_size < TACO_HEADER_TOTAL_SIZE) {
    return TACOZ_ERR_PARAM;
  }

  memset(buffer, 0, TACO_HEADER_TOTAL_SIZE);

  unsigned char payload[TACO_HEADER_PAYLOAD_SIZE];
  create_header_payload(meta, payload);

  uint32_t final_crc = (uint32_t)crc32(crc32(0L, Z_NULL, 0), payload, TACO_HEADER_PAYLOAD_SIZE);

  le32(buffer, ZIP_LFH_SIGNATURE);
  le16(buffer + 4, ZIP_VERSION_NEEDED);
  le16(buffer + 6, TACOZ_SET_UTF8_FLAG ? (1 << 11) : 0);
  le16(buffer + 8, 0);

  time_t now = time(NULL);
  uint16_t dos_time, dos_date;
  unix_time_to_dos(now, &dos_time, &dos_date);
  le16(buffer + 10, dos_time);
  le16(buffer + 12, dos_date);

  le32(buffer + 14, final_crc);
  le32(buffer + 18, TACO_HEADER_PAYLOAD_SIZE);
  le32(buffer + 22, TACO_HEADER_PAYLOAD_SIZE);
  le16(buffer + 26, TACO_HEADER_NAME_LEN);
  le16(buffer + 28, 0);

  memcpy(buffer + 30, TACO_HEADER_NAME, TACO_HEADER_NAME_LEN);
  memcpy(buffer + 41, payload, TACO_HEADER_PAYLOAD_SIZE);

  TACOZIP_DEBUG("Serialized header: %u entries, CRC32=0x%08x", meta->count, final_crc);
  return TACOZ_OK;
}

int tacozip_read_header(const char *zip_path, taco_meta_array_t *meta_out) {
  if (!zip_path || !meta_out)
    return TACOZ_ERR_PARAM;

  // Works for both ZIP32 and ZIP64 - TACO header is always at offset 0
  FILE *fp = fopen(zip_path, "rb");
  if (!fp)
    return TACOZ_ERR_IO;

  unsigned char buffer[TACO_HEADER_TOTAL_SIZE];
  size_t bytes_read = fread(buffer, 1, TACO_HEADER_TOTAL_SIZE, fp);
  fclose(fp);

  if (bytes_read != TACO_HEADER_TOTAL_SIZE)
    return TACOZ_ERR_IO;

  return tacozip_parse_header(buffer, TACO_HEADER_TOTAL_SIZE, meta_out);
}

int tacozip_update_header(const char *zip_path, const taco_meta_array_t *meta) {
  if (!zip_path || !meta)
    return TACOZ_ERR_PARAM;

  TACOZIP_DEBUG("Updating header in '%s'", zip_path);

  // Works for both ZIP32 and ZIP64
  FILE *fp = fopen(zip_path, "r+b");
  if (!fp)
    return TACOZ_ERR_IO;

  unsigned char payload[TACO_HEADER_PAYLOAD_SIZE];
  create_header_payload(meta, payload);

  uint32_t new_crc32 = (uint32_t)crc32(crc32(0L, Z_NULL, 0), payload, TACO_HEADER_PAYLOAD_SIZE);

  // Update payload in LFH at offset 41
  if (fseek(fp, 41, SEEK_SET) != 0) {
    fclose(fp);
    return TACOZ_ERR_IO;
  }

  if (fwrite(payload, 1, TACO_HEADER_PAYLOAD_SIZE, fp) != TACO_HEADER_PAYLOAD_SIZE) {
    fclose(fp);
    return TACOZ_ERR_IO;
  }

  // Update CRC32 in LFH at offset 14
  if (fseek(fp, 14, SEEK_SET) != 0) {
    fclose(fp);
    return TACOZ_ERR_IO;
  }

  unsigned char crc_bytes[4];
  le32(crc_bytes, new_crc32);
  if (fwrite(crc_bytes, 1, 4, fp) != 4) {
    fclose(fp);
    return TACOZ_ERR_IO;
  }

  // Update CRC32 in CD (handles both ZIP32 and ZIP64)
  int rc = find_cd_and_update_crc32(fp, new_crc32);
  if (rc != TACOZ_OK) {
    fclose(fp);
    return rc;
  }

  fclose(fp);
  return TACOZ_OK;
}

const char *tacozip_get_version(void) { 
  return TACOZIP_VERSION_STRING;
}

int tacozip_create(const char *zip_path, const char *const *src_files,
                   const char *const *arc_files, size_t num_files,
                   const taco_meta_array_t *meta) {
  if (!zip_path || !meta || num_files == 0)
    return TACOZ_ERR_PARAM;

  TACOZIP_DEBUG("Creating archive '%s' with %zu files", zip_path, num_files);

  // Pre-stat all files ONCE to get sizes
  zip_int64_t *file_sizes = malloc(num_files * sizeof(zip_int64_t));
  if (!file_sizes)
    return TACOZ_ERR_IO;

  for (size_t i = 0; i < num_files; i++) {
    struct stat st;
    if (stat(src_files[i], &st) != 0) {
      free(file_sizes);
      return TACOZ_ERR_IO;
    }
    file_sizes[i] = (zip_int64_t)st.st_size;
  }

  int error;
  zip_t *za = zip_open(zip_path, ZIP_CREATE | ZIP_TRUNCATE, &error);
  if (!za) {
    free(file_sizes);
    return TACOZ_ERR_LIBZIP;
  }

  if (add_header_to_archive(za, meta) != TACOZ_OK) {
    zip_discard(za);
    free(file_sizes);
    return TACOZ_ERR_LIBZIP;
  }

  for (size_t i = 0; i < num_files; i++) {
    // Use exact size - libzip won't stat again!
    zip_source_t *source = zip_source_file(za, src_files[i], 0, file_sizes[i]);
    if (!source) {
      zip_discard(za);
      free(file_sizes);
      return TACOZ_ERR_IO;
    }

    zip_int64_t index = zip_file_add(za, arc_files[i], source, ZIP_FL_OVERWRITE);
    if (index < 0) {
      zip_source_free(source);
      zip_discard(za);
      free(file_sizes);
      return TACOZ_ERR_LIBZIP;
    }

    zip_set_file_compression(za, (zip_uint64_t)index, ZIP_CM_STORE, 0);
  }

  free(file_sizes);

  if (zip_close(za) < 0)
    return TACOZ_ERR_LIBZIP;

  TACOZIP_DEBUG("Archive created successfully");
  return TACOZ_OK;
}