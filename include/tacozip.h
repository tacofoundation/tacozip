#ifndef TACOZIP_H
#define TACOZIP_H

/**
 * @file tacozip.h
 * @brief ZIP64 (STORE-only) writer with libzip backend and a fixed "TACO Header"
 *        Local File Header at byte 0. The header supports up to 7 metadata entries.
 *
 * ## Overview
 * - Always ZIP64: forces ZIP64 format regardless of file sizes for serialization consistency.
 * - STORE-only (method=0). No compression for maximum throughput.
 * - Uses libzip as the underlying ZIP implementation (no more custom ZIP code).
 * - A "TACO Header" entry is written first so its LFH appears at file start.
 *   This header appear in the Central Directory as a normal file entry.
 * - Up to 7 (offset,length) metadata pairs for external indices stored in header payload.
 * - No filename normalization in C; callers must pass sanitized archive names.
 *
 * ## Threading
 * - Functions are not thread-safe on the same zip_path concurrently.
 *
 * ## Large files
 * - Designed for large files: build with `_FILE_OFFSET_BITS=64`.
 *
 * ## Encoding
 * - If compiled with `TACOZ_SET_UTF8_FLAG=1`, the general purpose bit 11 is set
 *   (caller guarantees archive names are UTF-8). Otherwise, bit 11 is 0.
 *
 * ## Dependencies
 * - Requires libzip for all ZIP operations.
 *
 * ## ABI / Visibility
 * - Functions are exported with default visibility when building the shared lib.
 *   Define `TACOZIP_BUILD` when compiling the library itself.
 *
 * ## Debug Mode
 * - Runtime debug output controlled by TACOZIP_DEBUG environment variable
 * - Set TACOZIP_DEBUG=ON, TACOZIP_DEBUG=1, or TACOZIP_DEBUG=TRUE to enable
 * - Example: TACOZIP_DEBUG=ON ./my_program
 * - Debug messages print to stderr with minimal overhead when disabled
 *
 * ## Typical usage (C)
 * @code
 *   const char *src[] = {"/abs/a.bin", "/abs/b.bin"};
 *   const char *arc[] = {"a.bin", "sub/b.bin"};
 *   
 *   // Up to 7 metadata entries
 *   taco_meta_array_t meta = {
 *       .count = 2,
 *       .entries = {{1000, 500}, {2000, 750}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}
 *   };
 *   
 *   // Create archive with files and metadata
 *   int rc = tacozip_create("out.taco.zip", src, arc, 2, &meta);
 *   if (rc != TACOZ_OK) { handle error }
 *   
 *   // Update metadata entries
 *   meta.entries[0].offset = 1500;
 *   meta.entries[0].length = 600;
 *   rc = tacozip_update_header("out.taco.zip", &meta);
 *   
 *   // Append files to the archive (single or multiple)
 *   tacozip_append_entry_t entries[] = {
 *       {"/path/to/c.bin", "data/c.bin"},
 *       {"/path/to/d.bin", "data/d.bin"},
 *       {"/path/to/e.bin", "data/e.bin"}
 *   };
 *   rc = tacozip_append_files("out.taco.zip", entries, 3);
 *   
 *   // Append a single file (same API)
 *   tacozip_append_entry_t single_entry = {"/path/to/f.bin", "data/f.bin"};
 *   rc = tacozip_append_files("out.taco.zip", &single_entry, 1);
 *   
 *   // Replace an existing file in the archive
 *   rc = tacozip_replace_file("out.taco.zip", "a.bin", "/path/to/new_a.bin");
 * @endcode
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TACO Header Layout (variable size, minimum 160 bytes):
 *
 * The header entry is a regular ZIP entry but with special content:
 * - Entry name: "TACO_HEADER"
 * - Method: STORE (0)
 * - Content: binary payload with metadata pairs
 *
 * Header payload format:
 *  [0]      : uint8_t count (number of valid entries, 0-7)
 *  [1..3]   : padding (3 bytes for alignment)
 *  [4..115] : 7 pairs of uint64_le (offset, length) - total 112 bytes
 *
 * Total payload size: 116 bytes (4 + 7*16)
 */

#define TACO_HEADER_MAX_ENTRIES   7u
#define TACO_HEADER_PAYLOAD_SIZE  116u  /* 4 bytes header + 7*16 bytes pairs */
#define TACO_HEADER_NAME          "TACO_HEADER"
#define TACO_HEADER_NAME_LEN      11u

/** @brief Single metadata entry */
typedef struct {
    uint64_t offset;  /**< Absolute byte offset of external metadata. */
    uint64_t length;  /**< Length in bytes of external metadata.      */
} taco_meta_entry_t;

/** @brief Array of up to 7 metadata entries carried by the header. */
typedef struct {
    uint8_t count;                               /**< Number of valid entries (0-7). */
    taco_meta_entry_t entries[TACO_HEADER_MAX_ENTRIES]; /**< Metadata entries array. */
} taco_meta_array_t;

/** @brief Entry for append operations (single or batch) */
typedef struct {
    const char *src_path;  /**< Path to the source file on filesystem. */
    const char *arc_name;  /**< Name to use for the file in the archive. */
} tacozip_append_entry_t;

/* Export / visibility macro */
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

/**
 * @enum tacozip_status
 * @brief Return codes (0 = success, negative = error).
 */
enum {
    TACOZ_OK                =  0,  /**< Success. */
    TACOZ_ERR_IO            = -1,  /**< I/O error (open/read/write/close/flush). */
    TACOZ_ERR_LIBZIP        = -2,  /**< libzip error. */
    TACOZ_ERR_INVALID_HEADER = -3,  /**< Header bytes malformed or unexpected. */
    TACOZ_ERR_PARAM         = -4,  /**< Invalid argument(s). */
    TACOZ_ERR_NOT_FOUND     = -5,  /**< File not found in archive. */
    TACOZ_ERR_EXISTS        = -6   /**< File already exists in archive. */
};

/* ========================================================================== */
/*                             VERSION INFORMATION                           */
/* ========================================================================== */

/**
 * @brief Get the version string of the tacozip C library.
 *
 * This function returns a compile-time version string that identifies the 
 * version of the tacozip C library. The version string is defined by the
 * TACOZIP_VERSION_STRING macro, which is typically set during the build
 * process via CMake using the PROJECT_VERSION variable.
 *
 * @return Version string (e.g., "1.2.3").
 */
TACOZIP_EXPORT
const char* tacozip_get_version(void);

/* ========================================================================== */
/*                                 CORE API                                  */
/* ========================================================================== */

/**
 * @brief Create a ZIP64 archive with a TACO Header supporting up to 7 metadata entries.
 *
 * Creates a new archive using libzip backend with forced ZIP64 format and STORE compression.
 * The TACO Header entry is written first so its Local File Header appears at byte 0.
 * 
 * @param zip_path     Output path for the archive.
 * @param src_files    Array of absolute or relative filesystem paths (N elements).
 * @param arc_files    Array of archive names (N elements; used verbatim).
 * @param num_files    Number of files N.
 * @param meta         Metadata structure containing up to 7 entries (use 0,0 for unused entries).
 * @return             TACOZ_OK on success; negative error code otherwise.
 *
 * @note The function uses meta->count to determine how many entries are valid.
 * @note All files will be stored using STORE method (no compression).
 * @note Unused entries in meta->entries should be set to {0, 0}.
 *
 * @code
 * // Create archive with 2 metadata entries
 * taco_meta_array_t meta = {
 *     .count = 2,
 *     .entries = {{1000, 500}, {2000, 750}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}
 * };
 * int rc = tacozip_create("out.taco.zip", src, arc, 2, &meta);
 * @endcode
 */
TACOZIP_EXPORT
int tacozip_create(const char *zip_path,
                  const char * const *src_files,
                  const char * const *arc_files,
                  size_t num_files,
                  const taco_meta_array_t *meta);


/**
 * @brief Update all metadata entries in the header in place.
 *
 * Modifies the TACO Header entry in an existing archive without affecting
 * other files. The archive structure remains unchanged.
 *
 * @param zip_path     Path to an existing archive created by this library.
 * @param meta         Metadata structure containing up to 7 entries (use 0,0 for unused entries).
 * @return             TACOZ_OK on success; negative error code otherwise.
 *
 * @note The function uses meta->count to determine how many entries are valid.
 * @note This operation is optimized and bypasses libzip for fast access.
 * @note Unused entries in meta->entries should be set to {0, 0}.
 *
 * @code
 * // Update header with new metadata
 * taco_meta_array_t meta = {
 *     .count = 3,
 *     .entries = {{1500, 600}, {2000, 750}, {3000, 1000}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}
 * };
 * int rc = tacozip_update_header("archive.taco", &meta);
 * @endcode
 */
TACOZIP_EXPORT
int tacozip_update_header(const char *zip_path,
                        const taco_meta_array_t *meta);

/**
 * @brief Read all metadata entries from the header.
 *
 * Reads the TACO Header entry from an existing archive without affecting
 * other files. This function extracts the current metadata stored in the header.
 *
 * @param zip_path     Path to an existing archive created by this library.
 * @param meta_out     Output structure to receive metadata (caller must provide).
 * @return             TACOZ_OK on success; negative error code otherwise.
 *
 * @note The function reads all 7 entries. Unused entries will be set to (0, 0).
 * @note The meta_out->count field will contain the number of valid entries.
 * @note This operation is optimized and bypasses libzip for fast access.
 *
 * @code
 * // Read current header metadata
 * taco_meta_array_t meta;
 * int rc = tacozip_read_header("archive.taco", &meta);
 * if (rc == TACOZ_OK) {
 *     printf("Found %u metadata entries\n", meta.count);
 *     for (int i = 0; i < meta.count; i++) {
 *         printf("Entry %d: offset=%llu, length=%llu\n", 
 *                i, meta.entries[i].offset, meta.entries[i].length);
 *     }
 * }
 * @endcode
 */
TACOZIP_EXPORT
int tacozip_read_header(const char *zip_path,
                      taco_meta_array_t *meta_out);

/**
 * @brief Append one or more files to an existing TACO archive.
 *
 * Adds new files to an existing archive created by this library. This function
 * can append a single file or multiple files in a single operation. When appending
 * multiple files, this is much more efficient than multiple individual operations
 * as it only updates the Central Directory once.
 * 
 * The TACO Header and existing files remain unchanged. All new files will use 
 * STORE method (no compression) like all other files in the archive.
 *
 * @param zip_path    Path to an existing archive created by this library.
 * @param entries     Array of tacozip_append_entry_t specifying files to append.
 * @param num_entries Number of entries in the array (1 for single file append).
 * @return            TACOZ_OK on success; negative error code otherwise.
 *                    Returns TACOZ_ERR_EXISTS if any arc_name already exists in archive.
 *                    Returns TACOZ_ERR_IO if any src_path cannot be read.
 *
 * @note All arc_name values must not conflict with existing files in the archive.
 * @note This operation preserves the TACO Header and all metadata entries.
 * @note All appended files will use STORE compression method for consistency.
 * @note For single file append, pass num_entries=1 with a single entry.
 * @note For multiple files, this operation is atomic - either all files are
 *       appended successfully or none are (archive remains unchanged on error).
 *
 * @code
 * // Single file append
 * tacozip_append_entry_t entry = {"/path/to/file.bin", "data/file.bin"};
 * int rc = tacozip_append_files("archive.taco", &entry, 1);
 * 
 * // Multiple files append
 * tacozip_append_entry_t entries[] = {
 *     {"/path/to/file1.bin", "data/file1.bin"},
 *     {"/path/to/file2.bin", "data/file2.bin"},
 *     {"/path/to/file3.bin", "data/file3.bin"}
 * };
 * int rc = tacozip_append_files("archive.taco", entries, 3);
 * @endcode
 */
TACOZIP_EXPORT
int tacozip_append_files(const char *zip_path,
                        const tacozip_append_entry_t *entries,
                        size_t num_entries);

/**
 * @brief Replace a specific file in an existing TACO archive.
 *
 * Finds a file by its archive name and replaces it with content from a new 
 * source file. The TACO Header and other files remain unchanged. The replacement 
 * file will use STORE method (no compression) like all other files in the archive.
 *
 * @param zip_path     Path to an existing archive created by this library.
 * @param file_name    Name of the file in the archive to replace (exact match).
 * @param new_src_path Path to the new file that will replace the existing one.
 * @return             TACOZ_OK on success; negative error code otherwise.
 *                     Returns TACOZ_ERR_NOT_FOUND if file_name doesn't exist in archive.
 *
 * @note The file_name must match exactly as it was stored in the archive.
 * @note The new file will maintain the same archive name but with updated content.
 * @note This operation preserves the TACO Header and all metadata entries.
 */
TACOZIP_EXPORT
int tacozip_replace_file(const char *zip_path,
                        const char *file_name,
                        const char *new_src_path);

/**
 * @brief Trim archive from a specific point to the end (METADATA/ or COLLECTION.json only)
 *
 * Removes the specified target and everything that comes after it in the physical 
 * archive layout. This is an efficient operation that truncates the file instead 
 * of moving data around.
 * 
 * SAFETY RESTRICTIONS:
 * - Only accepts "METADATA/" or "COLLECTION.json" as targets
 * - Only works if no files exist after the target in the physical layout
 * - Designed for TACO's specific structure: DATA/ → METADATA/ → COLLECTION.json
 * 
 * TYPICAL USAGE:
 * - Remove old metadata: tacozip_trim_from("archive.taco", "METADATA/")
 * - Remove collection file: tacozip_trim_from("archive.taco", "COLLECTION.json")
 * - Then append new files with tacozip_append_files()
 *
 * @param zip_path Path to an existing TACO archive
 * @param target Either "METADATA/" (removes entire directory) or "COLLECTION.json"
 * @return TACOZ_OK on success; negative error code otherwise.
 *         TACOZ_ERR_PARAM if target is not whitelisted or operation unsafe
 *         TACOZ_ERR_NOT_FOUND if target doesn't exist in archive
 *
 * @note This operation truncates the physical file, making it very fast
 * @note For "METADATA/", removes ALL files/folders that start with "METADATA/"
 * @note Operation fails if any non-target files exist after the trim point
 * @note After trimming, you can use tacozip_append_files() to rebuild that section
 */
TACOZIP_EXPORT
int tacozip_trim_from(const char *zip_path, const char *target);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TACOZIP_H */