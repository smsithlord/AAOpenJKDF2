#ifndef LIBRETRO_ARCHIVE_H
#define LIBRETRO_ARCHIVE_H

#include <string>
#include <vector>

/* Output metadata from libretro_archive_resolve. Lets the caller populate the
 * GET_GAME_INFO_EXT struct with archive-aware fields without re-parsing the
 * input path itself. */
struct ArchiveResolveOut {
    std::string archive_path;        /* original .zip/.7z path; empty if input wasn't an archive */
    std::string archive_file;        /* entry name within the archive that was extracted */
    bool        file_in_archive = false;
};

/* If the input path points at a ZIP/7z and the core's valid_extensions don't
 * include "zip"/"7z", extract the first entry whose extension is in the core's
 * accepted set, write it to a per-host temp dir, and return that new path.
 *
 * If the core natively accepts the input path's extension (e.g. ROM is
 * already .n64, or ROM is .zip and core lists "zip"), the function returns
 * the input path unchanged and out_meta stays default-initialized.
 *
 * On extraction, the produced path is appended to extracted_temp_files so the
 * host can unlink it on shutdown, AND out_meta is populated with the archive
 * source info.
 *
 * Returns empty string only on hard failure (couldn't open archive, no usable
 * entry, write failed). */
std::string libretro_archive_resolve(const std::string& in_path,
                                     const char* core_valid_extensions,
                                     std::vector<std::string>& extracted_temp_files,
                                     ArchiveResolveOut& out_meta);

#endif
