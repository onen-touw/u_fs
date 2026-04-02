// udir_t.cpp
#include "uffs/udir.h"
#include "uffs/ufile.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <fnmatch.h>
#include <cerrno>


namespace ufo {

    udir_t::udir_t(const fs::path& path, bool create_if_not_exists)
        : m_path(normalize(path)), m_is_valid(false), m_last_error(0)
    {
        // if root path should be /spiffs, /sdcard, ..., and if relative (e.g: /sdcard/a), path cant be empty or just /
        if (m_path.empty())
        {
            m_is_valid = false;
            return;
        }

        auto fstp = get_fs_type(m_path);
#ifndef UFS_SUPPORT_UNKNOWN_MOUNTPOINT
        if (fstp == ufs_type_t::unknown)
        {
            m_is_valid = false;
            printf("1\n");
            return;
        }
#endif

        if (fstp != ufs_type_t::spiffs)
        {
            m_flags.set(flag_e::support_dir);
            printf("s\n");
        }
        else // if spiffs
        {
            m_is_valid = true;
            return;
        }

        // Проверяем существование директории
        if (exists_impl())
        {
            printf("ex\n");
            m_is_valid = true;
            return;
        }
        else if (create_if_not_exists)
        {
            if (::mkdir(m_path.c_str(), 0755) != 0)
            {
                m_last_error = errno;
                m_is_valid = false;
                return;
            }
            m_is_valid = true;
            return;
        }
        printf("2\n");
        // if after constructor we have empty interface it is cant be valid
        m_is_valid = false;
    }

    // ============================================================================
    // file-op
    // ============================================================================

    bool udir_t::remove_file(const fs::path& filename)
    {
        if (!m_is_valid) {
            m_last_error = EBADF;
            return false;
        }
        
        if (filename.empty()) {
            m_last_error = EINVAL;
            return false;
        }

        // user input: file  dont requare normalize because file it just xxx.xxx (no /)
        fs::path file_path = m_path / filename;

        struct stat st;
        if (stat(file_path.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                m_last_error = EISDIR;
                return false;
            }
        }

        if (::remove(file_path.c_str()) != 0) {
            m_last_error = errno;
            return false;
        }
        
        m_last_error = 0;
        return true;
    }

    bool udir_t::rename_file(const fs::path& old_name, const fs::path& new_name)
    {
        if (!m_is_valid) {
            m_last_error = EBADF;
            return false;
        }
        
        if (old_name.empty() || new_name.empty()) {
            m_last_error = EINVAL;
            return false;
        }
        
        // user input: file  dont requare normalize because file it just xxx.xxx (no /)
        fs::path old_path = m_path / old_name;
        fs::path new_path = m_path / new_name;
        
        if (!file_exists(old_path))
        {
            return false;
        }
        
        if (::rename(old_path.c_str(), new_path.c_str()) != 0) {
            m_last_error = errno;
            return false;
        }
        
        m_last_error = 0;
        return true;
    }

    bool udir_t::copy_file(const fs::path& src_name, const fs::path& dst_name, bool current_dir, size_t buffer_size) const {
        if (!m_is_valid) {
            m_last_error = EBADF;
            return false;
        }
        
        if (src_name.empty() || dst_name.empty()) {
            m_last_error = EINVAL;
            return false;
        }

        // user input: file  dont requare normalize because file it just xxx.xxx (no /)
        fs::path src_path = m_path / src_name;
        fs::path dst_path;
        if (current_dir)
        {
            dst_path = m_path / dst_name;
        }
        else
        {
            dst_path = dst_name.lexically_normal();
        }
        
        return ufile_t::copy(src_path.c_str(), dst_path.c_str(), buffer_size);
    }

    size_t udir_t::file_size(const fs::path & fname) const
    {
        if (!m_is_valid) {
            return 0;
        }

        // user input: file  dont requare normalize because file it just xxx.xxx (no /)
        fs::path full = m_path / fname;
        struct stat st;
        if (stat(full.c_str(), &st) == 0)
        {
            if (S_ISREG(st.st_mode))
            {
                size_t sz = st.st_size;
                return sz;
            }
        }
        return 0;
    }

    std::vector<std::string> udir_t::list_files(const char* pattern) const {
        std::vector<std::string> result;
        
        if (!m_is_valid) {
            return result;
        }

        DIR* dir = nullptr;

        if (!supports_directories())
        {
            dir = opendir(UFS_SPIFFS_MOUNTPOINT);
        }
        else
        {
            dir = opendir(m_path.c_str());
        }

        if (!dir) {
            return result;
        }
        
        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }

            // m_path normalized in constructor and entry->d_name just name
            fs::path full = m_path / entry->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode))
            {
                if (!pattern || fnmatch(pattern, entry->d_name, 0) == 0)
                {
                    result.push_back(entry->d_name);
                }
            }
        }

        closedir(dir);
        return result;
    }

    bool udir_t::file_exists(const fs::path &path) const
    {
        if (!m_is_valid || path.empty())
        {
            return false;
        }

        // m_path normalized in constructor and entry->d_name just name
        auto p = m_path / path;

        struct stat st;
        if (stat(p.c_str(), &st) != 0)
        {
            return false;
        }
        return S_ISREG(st.st_mode);
    }

    // stats-op
    std::pair<size_t, size_t> udir_t::item_count() const {
        size_t fcount = 0;
        size_t dcount = 0;

        if (!m_is_valid) {
            return {dcount, fcount};
        }

        DIR* dir = nullptr;

        if (!supports_directories()) 
        {
            dir = opendir(UFS_SPIFFS_MOUNTPOINT);
        } 
        else 
        {
            dir = opendir(m_path.c_str());
        }

        if (!dir) {
            return {dcount, fcount};
        }
        
        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || 
                strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            fs::path full = m_path / entry->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) == 0)
            {
                if (S_ISREG(st.st_mode))
                {
                    ++fcount;
                }
                else if (S_ISDIR(st.st_mode))
                {
                    ++dcount;
                }
            }
        }
        closedir(dir);

        return {dcount, fcount};
    }

    // ========================================================================
    // subdir-op
    // ========================================================================

    bool udir_t::create_subdir(const fs::path& name)
    {
        if (!m_is_valid)
        {
            m_last_error = EBADF;
            return false;
        }

        if (!supports_directories())
        {
            m_last_error = ENOTSUP;
            return false;
        }

        if (name.empty())
        {
            m_last_error = EINVAL;
            return false;
        }

        // user input: relative dir  dont requare normalize because dir it just xxx (no /, no .)
        fs::path full_path = m_path / name;

        // Создаем директорию
        if (mkdir(full_path.c_str(), 0755) != 0)
        {
            if (errno == EEXIST)
            {
                // Проверяем, что это действительно директория
                struct stat st;
                if (stat(full_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                {
                    m_last_error = 0;
                    return true; // Уже существует и это директория
                }
            }
            m_last_error = errno;
            return false;
        }

        m_last_error = 0;
        return true;
    }

    bool udir_t::remove_subdir(const fs::path& name, bool recursive)
    {
        if (!m_is_valid) {
            m_last_error = EBADF;
            return false;
        }

        if (!supports_directories()) {
            m_last_error = ENOTSUP;
            return false;
        }
        
        if (name.empty()) {
            m_last_error = EINVAL;
            return false;
        }

        // user input: relative dir  dont requare normalize because dir it just xxx (no /, no .)
        fs::path subdir_path = m_path / name;
        
        if (recursive)
        {
            udir_t subdir(subdir_path.c_str());
            return subdir.remove_recursive();            
        }
        
        // Проверяем, что это директория
        struct stat st;
        if (stat(subdir_path.string().c_str(), &st) != 0) {
            m_last_error = errno;
            return false;
        }
        
        if (!S_ISDIR(st.st_mode)) {
            m_last_error = ENOTDIR;
            return false;
        }
        
        // Проверяем, что директория пуста
        udir_t subdir(subdir_path.c_str());
        auto items = subdir.item_count();

        if (items.first > 0 || items.second > 0) {
            m_last_error = ENOTEMPTY;
            return false;
        }
        
        if (rmdir(subdir_path.c_str()) != 0) {
            m_last_error = errno;
            return false;
        }
        
        m_last_error = 0;
        return true;
    }

    bool udir_t::subdir_exists(const fs::path& name) const
    {
        if (!m_is_valid || name.empty()) {
            return false;
        }
        
        if (!supports_directories()) {
            return false;
        }
        
        // user input: relative dir  dont requare normalize because dir it just xxx (no /, no .)
        fs::path subdir_path = m_path / name;
        struct stat st;
        if (stat(subdir_path.c_str(), &st) != 0) {
            return false;
        }
        
        return S_ISDIR(st.st_mode);
    }
    

    // ============================================================================
    // recursive-op
    // ============================================================================

    std::vector<std::string> udir_t::list_recursive(bool relative_paths) const {
        std::vector<std::string> result;
        
        if (!m_is_valid) {
            return result;
        }
        
        if (!supports_directories())
        {
            return list_files();
        }

        walk_recursive(m_path, result, relative_paths);

        return result;
    }

    
    bool udir_t::copy_recursive(const fs::path& dst) const {
        if (!m_is_valid) {
            m_last_error = EBADF;
            return false;
        }
        
        if (!supports_directories()) {
            m_last_error = ENOTSUP;
            return false;
        }

        return copy_recursive_impl(m_path, dst.lexically_normal());
    }

    bool udir_t::copy_recursive_impl(const fs::path& src, const fs::path& dst) const {
        struct stat st;
        if (stat(src.c_str(), &st) != 0) {
            return false;
        }
        
        if (S_ISDIR(st.st_mode)) {
            // Создаем директорию назначения
            udir_t dst_dir(dst.c_str(), true);
            if (!dst_dir.is_valid())
            {
                return false;
            }

            // Копируем содержимое
            DIR* dir = opendir(src.c_str());
            if (!dir)
            {
                return false;
            }

            struct dirent* entry = nullptr;
            while ((entry = readdir(dir)) != nullptr) {
                if (strcmp(entry->d_name, ".") == 0 || 
                    strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                
                fs::path src_child = src / entry->d_name;
                fs::path dst_child = dst / entry->d_name;
                if (!copy_recursive_impl(src_child, dst_child)) {
                    closedir(dir);
                    return false;
                }
            }
            
            closedir(dir);
            return true;
        }
        else
        {
            return ufile_t::copy(src.string().c_str(), dst.string().c_str());
        }
    }

    bool udir_t::remove_recursive() {
        if (!m_is_valid) {
            m_last_error = EBADF;
            return false;
        }
        
        if (!supports_directories()) {
            m_last_error = ENOTSUP;
            return false;
        }
        bool r = remove_recursive_impl(m_path);
        m_is_valid = false;
        return r;
    }

    bool udir_t::remove_recursive_impl(const fs::path& path) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            return false;
        }
        
        if (S_ISDIR(st.st_mode)) {
            DIR* dir = opendir(path.c_str());
            if (!dir) {
                return false;
            }
            
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (strcmp(entry->d_name, ".") == 0 || 
                    strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                
                fs::path full = path / entry->d_name;
                if (!remove_recursive_impl(full)) {
                    closedir(dir);
                    return false;
                }
            }

            closedir(dir);
            return rmdir(path.c_str()) == 0;
        }
        else
        {
            return ::remove(path.c_str()) == 0;
        }
    }

    void udir_t::walk_recursive(const fs::path& current, 
                            std::vector<std::string>& result,
                            bool relative_paths) const {
       
        DIR* dir = opendir(current.c_str());
        if (!dir) {
            return;
        }
        
        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }

            fs::path full = current / entry->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) == 0)
            {

                if (relative_paths)
                {
                    fs::path relative = full.lexically_relative(m_path);
                    result.push_back(relative.string());
                }
                else
                {
                    result.push_back(full.string());
                }

                if (S_ISDIR(st.st_mode))
                {
                    walk_recursive(full, result, relative_paths);
                }
            }
        }

        closedir(dir);
    }
    
    // ============================================================================
    // Получение информации
    // ============================================================================

    std::vector<std::string> udir_t::list_subdirs() const
    {
        std::vector<std::string> result;

        if (!m_is_valid || !supports_directories())
        {
            return result;
        }

        DIR *dir = opendir(m_path.c_str());
        if (!dir)
        {
            return result;
        }

        struct dirent *entry = nullptr;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }

            fs::path full = m_path / entry->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            {
                result.push_back(entry->d_name);
            }
        }

        closedir(dir);
        return result;
    }
    
    std::vector<file_info_t> udir_t::list_details() const
    {
        std::vector<file_info_t> result;
        
        if (!m_is_valid) 
        {
            return result;
        }
        
        DIR* dir = nullptr;

        if (!supports_directories())
        {
            dir = opendir(UFS_SPIFFS_MOUNTPOINT);
        }
        else
        {
            dir = opendir(m_path.c_str());
        }

        if (!dir) {
            return result;
        }
        
        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || 
                strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            fs::path full = m_path / entry->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) == 0) {
                file_info_t info;
                info.name = entry->d_name;
                info.size = st.st_size;
                info.is_directory = S_ISDIR(st.st_mode);
                info.modified = st.st_mtime;
                result.push_back(info);
            }
        }
        closedir(dir);
        return result;
    }
    
} // namespace ufo
