/// @file ufile.h
/// @author onen-touw <your.email@example.com>
/// @version 1.0.0
/// @date 2026
/// @brief File operations class for ESP32 file systems
/// 
/// @details Provides RAII wrapper for FILE* operations with support for
///          different file systems (SPIFFS, LittleFS, FAT) on ESP32.
///          Supports move semantics and comprehensive error handling.
/// 
/// @copyright Copyright (c) 2026
/// @license MIT


#pragma once

#include <cstdio>
#include <string>
#include <cstddef>

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

#define UFS_SPIFFS_MOUNTPOINT "/spiffs"

namespace ufo {

/// @defgroup file_modes File Opening Modes
/// @brief File opening modes corresponding to fopen() modes
/// @{

/// @brief File opening modes
/// @details Correspond to the standard fopen modes with the addition of a binary flag
enum class file_mode {
    read_rb,            ///< "rb" - Read only, binary mode
    write_wb,           ///< "wb" - Write only, creates/overwrites, binary mode
    append_ab,          ///< "ab" - Append only, writes to end, binary mode
    read_write_r_b,     ///< "r+b" - Read and write, file must exist, binary mode
    write_read_w_b,     ///< "w+b" - Read and write, creates/overwrites, binary mode
    append_read_a_b     ///< "a+b" - Read and write, appends to end, binary mode
};

/// @} 


/// @defgroup seek_origins Seek Origins
/// @brief Reference points for seek operations
/// @{

/// @brief Seek origin for file positioning
enum class seek_origin {
    set = SEEK_SET,   ///< From beginning of file
    cur = SEEK_CUR,   ///< From current position
    end = SEEK_END    ///< From end of file
};

/// @} 


/// @class ufile_t ufile.h
/// @brief RAII wrapper for C FILE* operations
/// 
/// @details This class provides automatic resource management for file operations.
///          It wraps standard C file functions (fopen, fclose, fread, fwrite, etc.)
///          with RAII semantics and move support.
/// 
/// @section example_usage Example Usage
/// @code
/// // Basic file operations
/// ufile_t file("/spiffs/config.json", file_mode::read_rb);
/// if (file) {
///     char buffer[256];
///     size_t bytes = file.read(buffer, sizeof(buffer));
///     // ... process data
/// } // Auto-closed
/// 
/// // Move semantics
/// ufile_t create_file("/tmp/data.txt", file_mode::write_wb);
/// create_file.write("Hello", 5);
/// ufile_t moved_file = std::move(create_file); // create_file is now invalid
/// 
/// // Static utilities
/// if (ufile_t::exists("/spiffs/config.json")) {
///     ufile_t::copy("/spiffs/config.json", "/spiffs/config.json.bak");
/// }
/// @endcode
///
/// @section thread_safety Thread Safety
/// - Different instances for different files are safe to use
/// from different FreeRTOS tasks
/// - One instance is NOT safe to use from different tasks
/// without external synchronization (mutex)
/// - Individual method calls (read_rb/write) are atomic at the VFS level,
/// but the sequence of operations requires synchronization
/// 
/// @warning Not thread-safe for same instance across multiple tasks
///
/// @note All methods return a bool to indicate success/failure.
/// Detailed error information can be obtained via last_error()
/// 
/// @see ufile_t::open(), ufile_t::close(), ufile_t::read(), ufile_t::write()
class ufile_t {

protected:
    FILE* m_handle = nullptr;       ///< C file handle
    fs::path m_path;                ///< File path (normalized)
    mutable int m_last_error = 0;   ///< Last errno value

public:

    /// ========================================================================
    /// @name Constructors & Destructor
    /// @{

    /// @brief Default constructor
    /// Creates an invalid object, requiring a call to open()
    ufile_t() = default;
    
    /// @brief Constructor with path
    /// Creates an invalid object, requiring a call to open()
    /// @param path File path (in VFS ESP-IDF format, e.g. "/spiffs/config.txt")
    ufile_t(const fs::path& path) : m_path(path.lexically_normal()){}
    
    /// @brief Constructor that opens a file
    /// @param path Path to the file (in VFS ESP-IDF format, e.g. "/spiffs/config.txt")
    /// @param mode Open mode
    ufile_t(const fs::path&  path, file_mode mode);
    
    /// @brief Destructor
    /// Automatically closes the file if it was open.
    ~ufile_t();
    
    /// @cond DELETED
    ufile_t(const ufile_t&) = delete;
    ufile_t& operator=(const ufile_t&) = delete;
    /// @endcond

    /// @brief @brief Move constructor
    ufile_t(ufile_t&& other) noexcept;
    
    /// @brief Move assignment operator
    ufile_t& operator=(ufile_t&& other) noexcept;
    
    /// @} 
    /// ========================================================================

    /// ========================================================================
    /// @name File Operations
    /// @{

    /// @brief Open a file (if the object hasn't been opened previously)
    /// @param path Path to the file
    /// @param mode Open mode
    /// @return true if successful, false if an error occurred
    /// @details If file was already open, it will be closed first.
    bool open(const fs::path& path, file_mode mode);

    /// @brief Open the file (if the object hasn't been opened previously)
    /// @param mode Open mode
    /// @return true if successful, false if an error occurred
    /// @details If file was already open, it will be closed first.
    /// @pre Object must have been constructed with a path
    bool open(file_mode mode);
    
    /// @brief Close file
    /// Safe to call multiple times (repeat calls are ignored)
    void close();
    
    /// @brief Check if the file is open
    /// @return true if the file is open and ready for operations
    bool is_open() const { return m_handle != nullptr; }
    
    /// @} 
    /// ========================================================================

    /// ========================================================================
    /// @name Read/Write Operations
    /// @{

    /// @brief Read data from the file
    /// @param buffer Pointer to the buffer to read from
    /// @param size Buffer size (number of bytes to read)
    /// @return Number of bytes actually read (0 on error or EOF)
    size_t read(void* buffer, size_t size);
    
    /// @brief Write data to the file
    /// @param buffer Pointer to the data to write
    /// @param size Size of the data in bytes
    /// @return Number of bytes actually written
    size_t write(const void* buffer, size_t size);
    
    /// @brief Read one line (until '\n' or end of file)
    /// @param buffer Buffer for the line
    /// ​​@param max_len Maximum buffer length (including the null terminator)
    /// @return true if the line has been read, false on error or EOF
    /// @note The line is always null-terminated
    /// @note The '\n' character is included in the line (if found)
    bool getline(char* buffer, size_t max_len);
    
    /// @brief Read a line into std::string (convenient version)
    /// @param line String to output
    /// @param delimiter Line separator (default '\n')
    /// @return true if the line was read, false on error or EOF
    /// @warning May cause heap fragmentation; use with caution in embedded applications
    bool getline(std::string& line, char delimiter = '\n');
    
    /// @} 
    /// ========================================================================

    /// ========================================================================
    /// @name File Positioning
    /// @{

    /// @brief Check for end-of-file
    /// @return true if end-of-file is reached or the file is not open
    bool eof() const;
    
    /// @brief Get the file size in bytes
    /// @return File size, 0 on error (check is_open())
    size_t size() const;
    
    /// @brief Get the current position in the file
    /// @return The current position from the start, (size_t)-1 on error
    size_t tell() const;
    
    /// @brief Set the file position
    /// @param offset Offset
    /// @param origin Starting point (start, current position, end)
    /// @return true if successful
    bool seek(long offset, seek_origin origin = seek_origin::set);
    
    /// @brief Move to the beginning of the file
    /// @return true if successful
    bool rewind();
    
    /// @brief Flush file buffers to disk
    /// @return true on success, false on error
    /// @note Ensures all buffered data is written to physical storage
    bool flush();
    
    /// @} 
    /// ========================================================================
    
    /// @brief Copy a file
    /// @param src Path to the source file
    /// @param dst Path to the destination file
    /// @param buffer_size Buffer size for copying (default 4096 bytes)
    /// @return true if successful
    /// @note Static method, uses buffered copying
    /// to save memory
    static bool copy(const fs::path& src, const fs::path& dst, size_t buffer_size = 4096);
    
    /// @brief Check for file existence
    /// @param path Path to file
    /// @return true if file exists
    static bool exists(const fs::path& path);
    
    // ========================================================================
    /// @name Error Handling & Utilities
    /// @{

    /// @brief Get the last error
    /// @return The errno value after the last operation
    int last_error() const { return m_last_error; }
    
    /// @brief Get the path to the file (if given)
    const fs::path& path() const { return m_path; }
    
    /// @brief Check the validity of an object (can be used in if)
    explicit operator bool() const { return is_open(); }

    /// @} 
    /// ========================================================================
    
private:
    /// @brief Convert file_mode to string mode for fopen
    static const char* mode_to_string(file_mode mode);
};

} // namespace ufo