#ifndef TACOZIP_H
#define TACOZIP_H

/**
 * @file tacozip.h
 * @brief Regular ZIP (STORE-only) writer with libzip backend and TACO Header at byte 0
 *
 * ## Overview
 * - Regular ZIP format (4GB max) with STORE compression (method=0) for maximum throughput
 * - TACO Header (157 bytes) always at file offset 0
 * - Supports up to 7 metadata entries (offset, length pairs)
 *
 * ## Size Limits
 * - Maximum archive size: 4GB (enforced at API level)
 * - Maximum individual file size: 4GB
 * - Maximum entries: 65535
 *
 * ## Threading
 * - Functions are not thread-safe on the same zip_path concurrently
 *
 * ## Debug Mode
 * - Set TACOZIP_DEBUG=ON or TACOZIP_DEBUG=1 to enable runtime debug output
 * - Example: TACOZIP_DEBUG=ON ./my_program
 * - Debug messages print to stderr with minimal overhead when disabled
 *
 * ## Dependencies
 * - libzip for ZIP operations
 * - zlib for CRC32 calculation
 *
 * ## Typical usage
 * @code
 *   // Create archive
 *   const char *src[] = {"/path/a.bin", "/path/b.bin"};
 *   const char *arc[] = {"data/a.bin", "data/b.bin"};
 *
 *   taco_meta_array_t meta = {
 *       .count = 2,
 *       .entries = {{1000, 500}, {2000, 750}}
 *   };
 *
 *   tacozip_create("out.taco", src, arc, 2, &meta);
 *
 *   // Read header (local file)
 *   taco_meta_array_t read_meta;
 *   tacozip_read_header("out.taco", &read_meta);
 *
 *   // Or parse from buffer (HTTP, S3, etc)
 *   unsigned char buffer[200];
 *   http_get_range("https://cdn.com/data.taco", 0, 199, buffer);
 *   tacozip_parse_header(buffer, 200, &read_meta);
 *
 *   // Update metadata
 *   meta.entries[0].offset = 1500;
 *   tacozip_update_header("out.taco", &meta);
 *
 *   // Append files
 *   tacozip_append_entry_t entries[] = {
 *       {"/path/c.bin", "data/c.bin"},
 *       {"/path/d.bin", "data/d.bin"}
 *   };
 *   tacozip_append_files("out.taco", entries, 2);
 * @endcode
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TACO Header Physical Layout (157 bytes fixed):
 *
 *  Offset | Size | Content
 *  -------|------|--------------------------------------------------
 *  0      | 30   | Local File Header (ZIP LFH)
 *  30     | 11   | Filename: "TACO_HEADER"
 *  41     | 116  | Payload (metadata entries)
 *
 * Payload format (116 bytes):
 *  [0]      : uint8_t count (0-7 valid entries)
 *  [1..3]   : padding (reserved, must be 0)
 *  [4..115] : 7 × (uint64_t offset, uint64_t length) = 7 × 16 = 112 bytes
 */

#define TACO_HEADER_MAX_ENTRIES 7u
#define TACO_HEADER_PAYLOAD_SIZE 116u
#define TACO_HEADER_TOTAL_SIZE 157u
#define TACO_HEADER_NAME "TACO_HEADER"
#define TACO_HEADER_NAME_LEN 11u

/** @brief Single metadata entry */
typedef struct {
  uint64_t offset; /**< Byte offset in external file */
  uint64_t length; /**< Length in bytes */
} taco_meta_entry_t;

/** @brief Metadata array (up to 7 entries) */
typedef struct {
  uint8_t count; /**< Valid entries (0-7) */
  taco_meta_entry_t entries[TACO_HEADER_MAX_ENTRIES]; /**< Entry array */
} taco_meta_array_t;

/** @brief Entry for append operations */
typedef struct {
  const char *src_path; /**< Filesystem path to source file */
  const char *arc_name; /**< Name in archive */
} tacozip_append_entry_t;

/* Export/visibility */
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef TACOZIP_BUILD
#define TACOZIP_EXPORT __declspec(dllexport)
#else
#define TACOZIP_EXPORT __declspec(dllimport)
#endif
#else
#if __GNUC__ >= 4
#define TACOZIP_EXPORT __attribute__((visibility("default")))
#else
#define TACOZIP_EXPORT
#endif
#endif

/** @brief Return codes */
enum {
  TACOZ_OK = 0,                  /**< Success */
  TACOZ_ERR_IO = -1,             /**< I/O error */
  TACOZ_ERR_LIBZIP = -2,         /**< libzip error */
  TACOZ_ERR_INVALID_HEADER = -3, /**< Invalid header */
  TACOZ_ERR_PARAM = -4,          /**< Invalid parameters */
  TACOZ_ERR_NOT_FOUND = -5,      /**< File not found */
  TACOZ_ERR_EXISTS = -6,         /**< File exists */
  TACOZ_ERR_TOO_LARGE = -7       /**< Archive too large (>4GB) */  
};

/* ========================================================================== */
/*                             VERSION                                       */
/* ========================================================================== */

/**
 * @brief Get library version string
 * @return Version string (e.g., "1.2.3")
 */
TACOZIP_EXPORT
const char *tacozip_get_version(void);

/* ========================================================================== */
/*                          LOW-LEVEL API (I/O-FREE)                         */
/* ========================================================================== */

/**
 * @brief Parse TACO header from buffer (no I/O)
 *
 * Parses metadata from first 157 bytes of a TACO archive. Caller provides
 * the bytes via any I/O mechanism (file, HTTP, S3, mmap, etc).
 *
 * @param buffer     Buffer with at least 157 bytes
 * @param buffer_size Buffer size (must be >= 157)
 * @param meta_out   Output metadata structure
 * @return           TACOZ_OK or error code
 *
 * @note Validates LFH signature and filename
 * @note Thread-safe if different buffers used
 *
 * @code
 * unsigned char buf[200];
 * http_get_range("https://x.com/data.taco", 0, 199, buf);
 * taco_meta_array_t meta;
 * tacozip_parse_header(buf, 200, &meta);
 * @endcode
 */
TACOZIP_EXPORT
int tacozip_parse_header(const unsigned char *buffer, size_t buffer_size,
                         taco_meta_array_t *meta_out);

/**
 * @brief Serialize TACO header to buffer (no I/O)
 *
 * Generates complete 157-byte header with metadata and correct CRC32.
 * Caller handles writing the bytes via any I/O mechanism.
 *
 * @param meta       Metadata to serialize
 * @param buffer     Output buffer (must be >= 157 bytes)
 * @param buffer_size Buffer size (must be >= 157)
 * @return           TACOZ_OK or error code
 *
 * @note Always generates exactly 157 bytes
 * @note Automatically calculates CRC32
 * @note Thread-safe if different buffers used
 *
 * @code
 * taco_meta_array_t meta = {.count = 2, .entries = {{1000, 500}, {2000, 750}}};
 * unsigned char buf[157];
 * tacozip_serialize_header(&meta, buf, 157);
 * http_put_range("https://x.com/data.taco", 0, 156, buf);
 * @endcode
 */
TACOZIP_EXPORT
int tacozip_serialize_header(const taco_meta_array_t *meta,
                             unsigned char *buffer, size_t buffer_size);

/* ========================================================================== */
/*                       CONVENIENCE API (FILE WRAPPERS)                     */
/* ========================================================================== */

/**
 * @brief Read header from file
 *
 * Reads first 157 bytes from file and parses metadata.
 * For custom I/O (HTTP, S3, mmap), use tacozip_parse_header().
 *
 * @param zip_path   Path to TACO archive
 * @param meta_out   Output metadata
 * @return           TACOZ_OK or error code
 *
 * @code
 * taco_meta_array_t meta;
 * tacozip_read_header("archive.taco", &meta);
 * printf("Entries: %u\n", meta.count);
 * @endcode
 */
TACOZIP_EXPORT
int tacozip_read_header(const char *zip_path, taco_meta_array_t *meta_out);

/**
 * @brief Update header in file
 *
 * Efficient update of metadata - only writes changed bytes:
 * - 116 bytes payload at offset 41
 * - 4 bytes CRC32 in LFH at offset 14
 * - 4 bytes CRC32 in Central Directory
 *
 * For custom I/O, use tacozip_serialize_header() and write manually.
 *
 * @param zip_path   Path to existing TACO archive
 * @param meta       New metadata
 * @return           TACOZ_OK or error code
 *
 * @note Does NOT rewrite entire header
 * @note Also updates CD entry CRC32
 *
 * @code
 * taco_meta_array_t meta = {.count = 2, .entries = {{1500, 600}, {2000, 750}}};
 * tacozip_update_header("archive.taco", &meta);
 * @endcode
 */
TACOZIP_EXPORT
int tacozip_update_header(const char *zip_path, const taco_meta_array_t *meta);

/* ========================================================================== */
/*                            ARCHIVE OPERATIONS                             */
/* ========================================================================== */

/**
 * @brief Create new TACO archive
 *
 * Creates regular ZIP archive with TACO header at byte 0. All files use STORE
 * compression. Header is written first so it appears physically at start.
 *
 * @param zip_path   Output path
 * @param src_files  Array of source paths (N elements)
 * @param arc_files  Array of archive names (N elements)
 * @param num_files  Number of files
 * @param meta       Metadata (up to 7 entries)
 * @return           TACOZ_OK or error code
 *
 * @note Maximum archive size: 4GB
 *
 * @code
 * const char *src[] = {"/path/a.bin", "/path/b.bin"};
 * const char *arc[] = {"data/a.bin", "data/b.bin"};
 * taco_meta_array_t meta = {.count = 1, .entries = {{1000, 500}}};
 * tacozip_create("out.taco", src, arc, 2, &meta);
 * @endcode
 */
TACOZIP_EXPORT
int tacozip_create(const char *zip_path, const char *const *src_files,
                   const char *const *arc_files, size_t num_files,
                   const taco_meta_array_t *meta);

/**
 * @brief Append files to existing archive
 *
 * Adds one or more files efficiently (single CD update for batch).
 * Header and existing files remain unchanged.
 *
 * @param zip_path    Path to existing archive
 * @param entries     Array of entries to append
 * @param num_entries Number of entries
 * @return            TACOZ_OK or error code
 *
 * @note Returns TACOZ_ERR_EXISTS if any arc_name conflicts
 * @note Atomic - all files appended or none (with rollback on error)
 * @note Maximum archive size: 4GB
 *
 * @code
 * tacozip_append_entry_t entries[] = {
 *     {"/path/c.bin", "data/c.bin"},
 *     {"/path/d.bin", "data/d.bin"}
 * };
 * tacozip_append_files("archive.taco", entries, 2);
 * @endcode
 */
TACOZIP_EXPORT
int tacozip_append_files(const char *zip_path,
                         const tacozip_append_entry_t *entries,
                         size_t num_entries);

/**
 * @brief Trim archive from target to end
 *
 * Fast truncation operation. Only accepts "METADATA/" or "COLLECTION.json"
 * for safety. Fails if non-target files exist after trim point.
 *
 * @param zip_path Path to archive
 * @param target   "METADATA/" or "COLLECTION.json"
 * @return         TACOZ_OK or error code
 *
 * @note Truncates file - very fast
 * @note For "METADATA/", removes all files starting with "METADATA/"
 *
 * @code
 * tacozip_trim_from("archive.taco", "METADATA/");
 * // Then rebuild: tacozip_append_files(...)
 * @endcode
 */
TACOZIP_EXPORT
int tacozip_trim_from(const char *zip_path, const char *target);

#ifdef __cplusplus
}
#endif

#endif /* TACOZIP_H */