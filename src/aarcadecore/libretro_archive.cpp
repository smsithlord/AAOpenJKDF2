/*
 * libretro_archive — pass-through-friendly archive resolver for Libretro cores.
 *
 * Cores declare the file extensions they natively accept via
 * retro_system_info::valid_extensions (pipe-separated, e.g. "smc|sfc|zip").
 * Some cores list "zip" or "7z" because they can read those formats directly;
 * others can't and need an unpacked file path.
 *
 * This module's only job is to bridge that gap for ZIP. If the core can take
 * the .zip itself, we hand the original path back unchanged. Otherwise we open
 * the archive, find the first entry whose extension is in the core's accepted
 * list, and extract it to aarcadecore/libretro/extracted/. The host tracks the
 * extracted file so it can unlink at shutdown.
 *
 * 7z support is intentionally not in this file — it needs the LZMA SDK as a
 * separate dependency. Phase 5b.
 */

#include "libretro_archive.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include "unzip.h"
/* LZMA SDK 7z reader (Igor Pavlov, public domain). */
#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"
#include "7zFile.h"
}

#ifdef _WIN32
#include <direct.h>
#define ARCHIVE_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define ARCHIVE_MKDIR(p) mkdir((p), 0755)
#endif

namespace {

std::string to_lower(const std::string& s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

std::string ext_of(const std::string& path)
{
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    /* No directory separator past the dot. */
    if (path.find_first_of("/\\", dot) != std::string::npos) return "";
    return to_lower(path.substr(dot + 1));
}

std::string basename_no_ext(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name;
}

std::set<std::string> parse_valid_extensions(const char* exts)
{
    std::set<std::string> out;
    if (!exts) return out;
    std::string current;
    for (const char* p = exts; *p; ++p) {
        if (*p == '|') {
            if (!current.empty()) { out.insert(to_lower(current)); current.clear(); }
        } else {
            current.push_back(*p);
        }
    }
    if (!current.empty()) out.insert(to_lower(current));
    return out;
}

void ensure_dir_recursive(const std::string& path)
{
    if (path.empty()) return;
    std::string tmp;
    tmp.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        tmp.push_back((c == '/') ? '\\' : c);
        if ((c == '/' || c == '\\') && !tmp.empty())
            ARCHIVE_MKDIR(tmp.c_str());
    }
    ARCHIVE_MKDIR(tmp.c_str());
}

/* Extract one entry from `zip` (already positioned via unzLocate / unzGoTo*)
 * to `out_path`. Returns true on success. */
bool extract_current_entry(unzFile zip, const std::string& out_path, uLong file_size)
{
    if (unzOpenCurrentFile(zip) != UNZ_OK) return false;

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        unzCloseCurrentFile(zip);
        return false;
    }

    constexpr int CHUNK = 64 * 1024;
    std::vector<char> buf(CHUNK);
    uLong remaining = file_size;
    while (remaining > 0) {
        int want = (remaining > (uLong)CHUNK) ? CHUNK : (int)remaining;
        int got = unzReadCurrentFile(zip, buf.data(), want);
        if (got <= 0) {
            unzCloseCurrentFile(zip);
            return false;
        }
        out.write(buf.data(), got);
        remaining -= (uLong)got;
    }
    unzCloseCurrentFile(zip);
    return true;
}

} /* anonymous namespace */

/* ========================================================================
 * 7z extraction (LZMA SDK)
 *
 * Lifted shape from aarcade-source-client/mp/src/aarcade/client/c_libretroinstance.cpp:
 * open file → CFileInStream + CLookToRead2 buffered reader → SzArEx_Open →
 * walk db.NumFiles, find first non-dir entry whose extension matches → SzArEx_Extract
 * (which decompresses an entire 'block' at a time, then we slice the entry out
 * of that block via offset + outSizeProcessed) → write to disk.
 * ======================================================================== */

namespace {

/* CRC tables initialized once per process. The LZMA SDK requires this before
 * any 7z call. Idempotent on repeat. */
void ensure_7z_crc_init()
{
    static bool inited = false;
    if (!inited) { CrcGenerateTable(); inited = true; }
}

constexpr size_t kInputBufSize = 1 << 18; /* 256 KB read-ahead buffer */

/* Extract first matching entry from a .7z to out_path. Returns true on success.
 *
 * accepted: set of valid file extensions (lowercase, no dot). Empty set means
 *           "accept any non-directory entry".
 * picked_ext: receives the lowercase extension (no dot) of the chosen entry,
 *             so the caller can name the temp file appropriately. */
bool extract_first_7z_match(const std::string& in_path,
                            const std::set<std::string>& accepted,
                            const std::string& out_path_template_no_ext,
                            std::string& out_path_actual,
                            std::string& picked_ext)
{
    ensure_7z_crc_init();

    CFileInStream archiveStream;
    CLookToRead2 lookStream;
    CSzArEx db;
    ISzAlloc allocImp     = { SzAlloc, SzFree };
    ISzAlloc allocTempImp = { SzAllocTemp, SzFreeTemp };

    if (InFile_Open(&archiveStream.file, in_path.c_str()) != 0) {
        printf("Libretro: archive — failed to open 7z '%s'\n", in_path.c_str());
        return false;
    }
    FileInStream_CreateVTable(&archiveStream);
    LookToRead2_CreateVTable(&lookStream, False);
    lookStream.buf = (Byte*)ISzAlloc_Alloc(&allocImp, kInputBufSize);
    if (!lookStream.buf) {
        File_Close(&archiveStream.file);
        return false;
    }
    lookStream.bufSize    = kInputBufSize;
    lookStream.realStream = &archiveStream.vt;
    LookToRead2_INIT(&lookStream);

    SzArEx_Init(&db);
    SRes res = SzArEx_Open(&db, &lookStream.vt, &allocImp, &allocTempImp);
    if (res != SZ_OK) {
        printf("Libretro: archive — SzArEx_Open failed (%d)\n", res);
        ISzAlloc_Free(&allocImp, lookStream.buf);
        File_Close(&archiveStream.file);
        return false;
    }

    /* Walk entries to find a usable file. */
    UInt32 found_index = (UInt32)-1;
    std::string found_name;
    for (UInt32 i = 0; i < db.NumFiles; ++i) {
        if (SzArEx_IsDir(&db, i)) continue;
        size_t name_len = SzArEx_GetFileNameUtf16(&db, i, NULL);
        std::vector<UInt16> name16(name_len);
        SzArEx_GetFileNameUtf16(&db, i, name16.data());
        /* UTF-16 → ASCII collapse; sufficient for ROM filenames in practice. */
        std::string name;
        name.reserve(name_len);
        for (size_t j = 0; j + 1 < name_len; ++j) name.push_back((char)name16[j]);

        std::string e = ext_of(name);
        if (e.empty()) continue;
        if (accepted.empty() || accepted.count(e)) {
            found_index = i;
            found_name  = name;
            picked_ext  = e;
            break;
        }
    }

    if (found_index == (UInt32)-1) {
        printf("Libretro: archive — no acceptable entry in 7z '%s'\n", in_path.c_str());
        SzArEx_Free(&db, &allocImp);
        ISzAlloc_Free(&allocImp, lookStream.buf);
        File_Close(&archiveStream.file);
        return false;
    }

    /* SzArEx_Extract decompresses the block containing the entry. Multiple
     * entries from the same block reuse outBuffer if blockIndex matches. */
    UInt32 blockIndex = 0xFFFFFFFFu;
    Byte*  outBuffer = NULL;
    size_t outBufferSize = 0;
    size_t offset = 0;
    size_t outSizeProcessed = 0;

    res = SzArEx_Extract(&db, &lookStream.vt, found_index,
                         &blockIndex, &outBuffer, &outBufferSize,
                         &offset, &outSizeProcessed,
                         &allocImp, &allocTempImp);
    if (res != SZ_OK) {
        printf("Libretro: archive — SzArEx_Extract failed (%d)\n", res);
        SzArEx_Free(&db, &allocImp);
        ISzAlloc_Free(&allocImp, lookStream.buf);
        File_Close(&archiveStream.file);
        return false;
    }

    out_path_actual = out_path_template_no_ext + "." + picked_ext;
    std::ofstream out(out_path_actual, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        printf("Libretro: archive — failed to open output '%s'\n", out_path_actual.c_str());
        ISzAlloc_Free(&allocImp, outBuffer);
        SzArEx_Free(&db, &allocImp);
        ISzAlloc_Free(&allocImp, lookStream.buf);
        File_Close(&archiveStream.file);
        return false;
    }
    out.write((const char*)(outBuffer + offset), (std::streamsize)outSizeProcessed);
    out.close();

    ISzAlloc_Free(&allocImp, outBuffer);
    SzArEx_Free(&db, &allocImp);
    ISzAlloc_Free(&allocImp, lookStream.buf);
    File_Close(&archiveStream.file);

    printf("Libretro: archive — extracted '%s' (%zu bytes) -> '%s'\n",
           found_name.c_str(), outSizeProcessed, out_path_actual.c_str());
    return true;
}

} /* anonymous namespace */

std::string libretro_archive_resolve(const std::string& in_path,
                                     const char* core_valid_extensions,
                                     std::vector<std::string>& extracted_temp_files,
                                     ArchiveResolveOut& out_meta)
{
    std::set<std::string> accepted = parse_valid_extensions(core_valid_extensions);
    std::string in_ext = ext_of(in_path);

    /* Pass-through: input isn't an archive we know how to handle, OR core
     * takes the input extension directly. */
    if (in_ext != "zip" && in_ext != "7z") return in_path;
    if (accepted.count(in_ext)) {
        printf("Libretro: archive — core accepts '.%s' natively, passing through\n", in_ext.c_str());
        return in_path;
    }

    /* 7z path. */
    if (in_ext == "7z") {
        ensure_dir_recursive("aarcadecore/libretro/extracted");
        std::string template_no_ext = "aarcadecore/libretro/extracted/" + basename_no_ext(in_path);
        std::string out_path, picked_ext;
        if (!extract_first_7z_match(in_path, accepted, template_no_ext, out_path, picked_ext)) {
            return "";
        }
        extracted_temp_files.push_back(out_path);
        out_meta.archive_path     = in_path;
        /* archive_file is the entry name (basename only — already stripped of dirs
         * by extract_first_7z_match's caller-facing contract). We didn't preserve
         * the entry name across the helper boundary; reconstruct from out_path's
         * basename to keep the meta useful without rewiring the helper. */
        size_t slash = out_path.find_last_of("/\\");
        out_meta.archive_file     = (slash == std::string::npos) ? out_path : out_path.substr(slash + 1);
        out_meta.file_in_archive  = true;
        return out_path;
    }

    /* Walk the zip looking for the first entry whose extension the core accepts. */
    unzFile zip = unzOpen(in_path.c_str());
    if (!zip) {
        printf("Libretro: archive — failed to open '%s'\n", in_path.c_str());
        return "";
    }

    if (unzGoToFirstFile(zip) != UNZ_OK) {
        unzClose(zip);
        printf("Libretro: archive — empty zip '%s'\n", in_path.c_str());
        return "";
    }

    char entry_name[1024];
    unz_file_info info;
    std::string picked_name;
    uLong picked_size = 0;

    do {
        if (unzGetCurrentFileInfo(zip, &info, entry_name, sizeof(entry_name),
                                  nullptr, 0, nullptr, 0) != UNZ_OK)
            continue;
        std::string name(entry_name);
        std::string ext = ext_of(name);
        if (ext.empty()) continue;
        /* If core declared no extensions (rare), accept the first entry. */
        if (accepted.empty() || accepted.count(ext)) {
            picked_name = name;
            picked_size = info.uncompressed_size;
            break;
        }
    } while (unzGoToNextFile(zip) == UNZ_OK);

    if (picked_name.empty()) {
        unzClose(zip);
        printf("Libretro: archive — no entry in '%s' matched core extensions (%s)\n",
               in_path.c_str(), core_valid_extensions ? core_valid_extensions : "(none)");
        return "";
    }

    /* Build output path: aarcadecore/libretro/extracted/<basename>.<entry-ext>
     * (basename derived from the archive, not the entry, so user-facing names
     * stay intuitive). Strip any leading directory components from the entry
     * to avoid arbitrary path writes. */
    std::string entry_basename = picked_name;
    size_t slash = entry_basename.find_last_of("/\\");
    if (slash != std::string::npos) entry_basename = entry_basename.substr(slash + 1);

    std::string out_dir = "aarcadecore/libretro/extracted";
    ensure_dir_recursive(out_dir);
    std::string out_path = out_dir + "/" + basename_no_ext(in_path) + "." + ext_of(entry_basename);

    if (!extract_current_entry(zip, out_path, picked_size)) {
        unzClose(zip);
        std::remove(out_path.c_str());
        printf("Libretro: archive — failed to extract '%s' to '%s'\n",
               picked_name.c_str(), out_path.c_str());
        return "";
    }
    unzClose(zip);

    extracted_temp_files.push_back(out_path);
    out_meta.archive_path    = in_path;
    out_meta.archive_file    = entry_basename;  /* dir-stripped entry name */
    out_meta.file_in_archive = true;
    printf("Libretro: archive — extracted '%s' (%lu bytes) -> '%s'\n",
           picked_name.c_str(), (unsigned long)picked_size, out_path.c_str());
    return out_path;
}
