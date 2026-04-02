 /// @file udir.h
 /// @author Your Name <your.email@example.com>
 /// @version 1.0.0
 /// @date 2026
 /// @brief Directory operations class for ESP32 file systems
 /// 
 /// @details Provides comprehensive directory and file operations for
 ///          different file systems on ESP32 (SPIFFS, LittleFS, FAT).
 ///          Handles FS-specific features like SPIFFS flat structure.
 /// 
 /// @note SPIFFS Limitations:
 ///       - No real directories (emulated via prefixes)
 ///       - create_subdir() and remove_subdir() return false
 ///       - list_subdirs() always returns empty
 /// 
 /// @copyright Copyright (c) 2026
 /// @license MIT

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstddef>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <fnmatch.h>
#include <cerrno>
#include "u_sys/btflg.h"
#include <filesystem>
#include "ufile.h"

/// @cond INTERNAL
/// works with std::filesystem
#if __has_include(<filesystem>)
    #include <filesystem>
    namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
    #include <experimental/filesystem>
    namespace fs = std::experimental::filesystem;
#else
    #error "filesystem not available"
#endif
/// @endcond

#define UFS_SUPPORT_UNKNOWN_MOUNTPOINT

namespace ufo {

/// @defgroup fs_types File System Types
/// @brief Supported file system type enumeration
/// @{


/// @enum ufs_type_t
/// @brief Supported file system types
enum class ufs_type_t 
{
    fat,
    spiffs,
    littlefs,
    unknown,
};

/// @}


/// @defgroup file_info File Information Structure
/// @brief Structure for detailed file/directory metadata
/// @{

/// @struct file_info_t
/// @brief Detailed file/directory information
struct file_info_t
{
    std::string name;  
    size_t size; 
    bool is_directory;
    time_t modified;
};

/// @}



/// @class udir_t udir.h
/// @brief Directory operations class for ESP32 file systems
/// 
/// @section example_basic Basic Usage
/// @code
/// // Create directory object (auto-create if not exists)
/// udir_t dir("/spiffs/config", true);
/// if (dir.is_valid()) {
///     // Create subdirectory
///     dir.create_subdir("backup");
///     
///     // List all JSON files
///     auto files = dir.list_files("*.json");
///     for (const auto& file : files) {
///         printf("Found: %s\n", file.c_str());
///     }
/// }
/// @endcode
///
/// Provides an interface for operations on files and subdirectories 
/// taking into account the features of different file systems (SPIFFS, LittleFS, FAT). 
/// 
/// @note Features: 
/// - SPIFFS: flat structure, directories are emulated through prefixes 
/// - LittleFS/FAT: full directory hierarchy 
/// - Does not support copying and moving between different file systems 
/// 
/// @warning The class is not thread safe. For use from different 
/// tasks require external synchronization. 
///
/// @see ufile_t
class udir_t {

    enum class flag_e
    {
        is_valid,
        is_root,
        support_dir,
    };

private:
    fs::path m_path; 
    bool m_is_valid; 
    ufo::bit_flag_t<uint8_t> m_flags;
    mutable int m_last_error;     
    
public:
    /// ========================================================================
    /// @name Constructors & Destructor
    /// @{

    /// @brief Constructor 
    /// @param path Path to the directory (for example, "/spiffs/config") 
    /// @param create_if_not_exists Create a directory if it does not exist 
    /// @note If create_if_not_exists = false and the directory does not exist, 
    /// the object will be in an invalid state (is_valid() = false) 
    explicit udir_t(const fs::path& path, bool create_if_not_exists = false);

    udir_t(udir_t&& other) noexcept 
        : m_path(std::move(other.m_path)), m_is_valid(other.m_is_valid), 
        m_flags(other.m_flags), m_last_error(other.m_last_error) 
    {
        other.m_is_valid = false;
        other.m_flags.upd(0);
    }
    udir_t& operator=(udir_t&& other) noexcept
        {
            if (&other != this)
            {
                m_path = std::move(other.m_path);
                m_is_valid = other.m_is_valid;
                m_flags = other.m_flags;
                m_last_error = other.m_last_error; 
    
                other.m_is_valid = false;
                other.m_flags.upd(0);
            }
            return *this;
        }

    /// @cond DELETED
    udir_t(const udir_t&) = delete;
    udir_t& operator=(const udir_t&) = delete;
    /// @endcond

    ///  @}
    /// ========================================================================

    /// ========================================================================
    /// @name State Information
    /// @{

    /// @brief Check object validity 
    /// @return true if the directory exists and is accessible
    bool is_valid() const { return m_is_valid; }
    
    /// @brief Get directory path
    const fs::path& path() const { return m_path; }
    
    /// @brief Get the latest error (errno)
    int last_error() const { return m_last_error; }
    
    ///  @}
    /// ========================================================================


    /// ========================================================================
    /// @name Subdirectory Operations
    /// @{

    /// @brief Create a subdirectory 
    /// @param name Subdirectory name (only). Does not support recursive 
    /// creation of a directory like /a/b/c
    /// @param create_parents Whether to create parent directories 
    /// @return true if successful or the directory already exists 
    /// @note Always returns false on SPIFFS (not supported)
    bool create_subdir(const fs::path& name);
    
    /// @brief Delete subdirectory. Must be empty if not use recursive flag
    /// @param name Subdirectory name (only).
    /// @param recursive Delete directory with all contents
    /// @return true if successful or the directory already exists 
    /// @note Always returns false on SPIFFS (not supported)
    bool remove_subdir(const fs::path& name, bool recursive = false);
    
    /// @brief Check if subdirectory exists
    /// @param name Subdirectory name (only).
    /// @return true if successful or the directory already exists 
    /// @note Always returns false on SPIFFS (not supported)
    bool subdir_exists(const fs::path& name) const;

    ///  @}
    /// ========================================================================
    
    /// ========================================================================
    /// @name File Operations
    /// @{

    /// @brief Get a file from this directory
    /// @param name Name (only) of file in this directory
    /// @return Returns ufile_t in unopened state
    ufile_t get_file(const fs::path &name)
    {
        if (!file_exists(name))
        {
            return ufile_t();
        }

        auto full = m_path / name;
        
        return ufile_t(full.c_str());
    }

    /// @brief Check if file exists in this directory
    /// @param name Path relative to this directory
    bool file_exists(const fs::path &name) const;

    /// @brief Delete file 
    /// @param name Name (only) of file in this directory
    /// @return true if successful 
    /// @note On SPIFFS the file must be closed before deleting
    bool remove_file(const fs::path& name);

    /// @brief Rename the file 
    /// @param old_name Name (only) of file in this directory
    /// @param new_name Name (only) of new file that be located in this directory
    /// @return true if successful 
    /// @note On SPIFFS the file must be closed before renaming
    bool rename_file(const fs::path & old_name, const fs::path & new_name);

    /// @brief Copy file 
    /// @param src_name Name (only) of file in this directory
    /// @param dst_name Name (only) if file will be in this directory, otherwise write full path and set current_dir flag
    /// @param current_dir If you want to copy file into this directory
    /// @param buffer_size Size of buffer to copy (default 4096) 
    /// @return true if successful
    bool copy_file(const fs::path & src_name, const fs::path & dst_name, bool current_dir = true, size_t buffer_size = 4096) const;

    /// @brief Get file valid size.
    /// @param name Name (only) of file in this directory
    size_t file_size(const fs::path& name) const;

    ///  @}
    /// ========================================================================
    
    /// ========================================================================
    /// @name Listing and Information
    /// @{

    /// @brief Get a list of files in a directory
    /// @param pattern Filter by name (wildcard, for example "*.json")
    /// @return std::vector<std::string> with file names
    std::vector<std::string> list_files(const char* pattern = nullptr) const;

    /// @brief Get folder item count
    /// @return { dcount, fcount } 
    std::pair<size_t, size_t> item_count() const;
    
    /// @brief Get a list of subdirectories
    /// @return A vector with subdirectory names
    /// @note On SPIFFS, always returns an empty list
    std::vector<std::string> list_subdirs() const;

    /// @brief Get detailed information about the contents of this directory
    /// @return std::vector<std::string> with details
    std::vector<file_info_t> list_details() const;

    /// @brief Recursively get the paths of the contents of this directory
    /// @param relative_paths If true paths relative to this directory, otherwise full paths
    /// @return std::vector<std::string> - list of pathes
    /// @note On SPIFFS, always @ref call list_files()
    std::vector<std::string> list_recursive(bool relative_paths) const;

    ///  @}
    /// ========================================================================
    
    /// ========================================================================
    /// @name Recursive Operations
    /// @{

    /// @brief Recursively copies the contents of this directory
    /// @param dst full path to new directory
    /// @note On SPIFFS, always do nothing
    bool copy_recursive(const fs::path& dst) const;
    
    /// @brief Recursively deletes the contents of this directory
    /// @return true if successful
    /// @note On SPIFFS, always do nothing
    bool remove_recursive();

    ///  @}
    /// ========================================================================
    
    
    /// @brief Check if this dirrectory support directory
    /// @note Only spiffs are not support
    bool supports_directories() const {return m_flags.get(flag_e::support_dir);}

private:
    bool copy_recursive_impl(const fs::path& src, const fs::path& dst) const;
    bool remove_recursive_impl(const fs::path& path);

    void walk_recursive(const fs::path& current, 
                            std::vector<std::string>& result,
                            bool relative_paths) const;

public:

    /// @brief Define fs type
    static ufs_type_t get_fs_type(const fs::path& path) {
        std::string str = path.string();
        
        if (str.find("/spiffs") == 0) return ufs_type_t::spiffs;
        if (str.find("/littlefs") == 0) return ufs_type_t::littlefs;
        if (str.find("/sdcard") == 0) return ufs_type_t::fat;
        if (str.find("/sdmmc") == 0) return ufs_type_t::fat;
        return ufs_type_t::unknown;
    }

private:
    bool exists_impl() const {
        struct stat st;
        if (stat(m_path.c_str(), &st) != 0) {
            return false;
        }
        return S_ISDIR(st.st_mode);
    }

    static fs::path normalize(const fs::path& path)  {
        std::string str = path.string();
        
        // Удаляем trailing slash (кроме корня)
        if (str.length() > 1 && str.back() == '/') {
            str.pop_back();
        }
        
        return fs::path(str).lexically_normal();
    }
};

} // namespace ufo