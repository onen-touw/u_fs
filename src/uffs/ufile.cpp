#include "uffs/ufile.h"
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

namespace ufo {

// ============================================================================
// constructors
// ============================================================================

ufile_t::ufile_t(const fs::path& path, file_mode mode) {
    open(path, mode);
}

ufile_t::~ufile_t() {
    close();
}

ufile_t::ufile_t(ufile_t&& other) noexcept
    : m_handle(other.m_handle)
    , m_path(std::move(other.m_path))
    , m_last_error(other.m_last_error)
{
    other.m_handle = nullptr;
}

ufile_t& ufile_t::operator=(ufile_t&& other) noexcept {
    if (this != &other) {
        close();
        
        m_handle = other.m_handle;
        m_path = std::move(other.m_path);
        m_last_error = other.m_last_error;
        
        other.m_handle = nullptr;
    }
    return *this;
}

// ============================================================================
// open/close
// ============================================================================

bool ufile_t::open(const fs::path& path, file_mode mode) {
    close();
    
    if (path.empty()) {
        m_last_error = EINVAL;
        return false;
    }
    
    const char* mode_str = mode_to_string(mode);
    m_handle = fopen(path.c_str(), mode_str);

    if (m_handle)
    {
        m_path = path;
        m_last_error = 0;
        return true;
    }
    else
    {
        m_last_error = errno;
        return false;
    }
}

bool ufile_t::open(file_mode mode) {
    close();
    
    if (m_path.empty()) {
        m_last_error = EINVAL;
        return false;
    }
    
    const char* mode_str = mode_to_string(mode);
    m_handle = fopen(m_path.c_str(), mode_str);

    if (m_handle)
    {
        m_last_error = 0;
        return true;
    }
    else
    {
        m_last_error = errno;
        return false;
    }
}

//+
void ufile_t::close() {
    if (m_handle) {
        fclose(m_handle);
        m_handle = nullptr;
        m_path.clear();
    }
}

// ============================================================================
// r/w
// ============================================================================

size_t ufile_t::read(void* buffer, size_t size) {
    if (!m_handle || !buffer) {
        m_last_error = m_handle ? EINVAL : EBADF;
        return 0;
    }
    
    size_t bytes_read = fread(buffer, 1, size, m_handle);
    
    if (bytes_read == 0 && ferror(m_handle)) {
        m_last_error = errno;
        return 0;
    }
    
    m_last_error = 0;
    return bytes_read;
}
//+
size_t ufile_t::write(const void* buffer, size_t size) {
    if (!m_handle || !buffer) {
        m_last_error = m_handle ? EINVAL : EBADF;
        return 0;
    }
    
    size_t bytes_written = fwrite(buffer, 1, size, m_handle);
    
    if (bytes_written != size && ferror(m_handle)) {
        m_last_error = errno;
        return bytes_written;
    }
    
    m_last_error = 0;
    return bytes_written;
}

// ============================================================================
// strings
// ============================================================================

bool ufile_t::getline(char* buffer, size_t max_len) {
    if (!m_handle || !buffer || max_len == 0) {
        m_last_error = m_handle ? EINVAL : EBADF;
        return false;
    }
    
    if (fgets(buffer, static_cast<int>(max_len), m_handle) != nullptr) {
        m_last_error = 0;
        return true;
    }
    
    // Проверяем, это ошибка или EOF
    if (feof(m_handle)) {
        m_last_error = 0;  // EOF не считается ошибкой
    } else {
        m_last_error = errno;
    }
    
    return false;
}

bool ufile_t::getline(std::string& line, char delimiter) {
    line.clear();
    
    if (!m_handle) {
        m_last_error = EBADF;
        return false;
    }
    
    char ch;
    bool found = false;
    
    while (read(&ch, 1) == 1) {
        line.push_back(ch);
        if (ch == delimiter) {
            found = true;
            break;
        }
    }
    
    if (!found && line.empty()) {
        return false;  // EOF без данных
    }
    
    return true;
}

// ============================================================================
// pos and stat
// ============================================================================

bool ufile_t::eof() const {
    if (!m_handle) {
        return true;
    }
    return feof(m_handle) != 0;
}

size_t ufile_t::size() const {
    if (!m_handle) {
        return 0;
    }
    
    // Сохраняем текущую позицию
    long current_pos = ftell(m_handle);
    if (current_pos == -1) {
        m_last_error = errno;
        return 0;
    }
    
    // Перемещаемся в конец
    if (fseek(m_handle, 0, SEEK_END) != 0) {
        m_last_error = errno;
        return 0;
    }
    
    // Получаем размер
    long file_size = ftell(m_handle);
    if (file_size == -1) {
        m_last_error = errno;
        return 0;
    }
    
    // Восстанавливаем позицию
    fseek(m_handle, current_pos, SEEK_SET);
    
    m_last_error = 0;
    
    return file_size;
}

size_t ufile_t::tell() const {
    if (!m_handle) {
        m_last_error = EBADF;
        return static_cast<size_t>(-1);
    }
    
    long pos = ftell(m_handle);
    if (pos == -1) {
        m_last_error = errno;
        return static_cast<size_t>(-1);
    }
    
    return static_cast<size_t>(pos);
}

bool ufile_t::seek(long offset, seek_origin origin) {
    if (!m_handle) {
        m_last_error = EBADF;
        return false;
    }
    
    int whence;
    switch (origin) {
        case seek_origin::set: whence = SEEK_SET; break;
        case seek_origin::cur: whence = SEEK_CUR; break;
        case seek_origin::end: whence = SEEK_END; break;
        default: 
            m_last_error = EINVAL;
            return false;
    }
    
    if (fseek(m_handle, offset, whence) != 0) {
        m_last_error = errno;
        return false;
    }
    
    // При перемещении позиции кэш размера остается валидным
    // (размер файла не изменился)
    m_last_error = 0;
    return true;
}

bool ufile_t::rewind() {
    if (!m_handle) {
        m_last_error = EBADF;
        return false;
    }
    
    ::rewind(m_handle);  // Стандартный rewind не возвращает ошибку
    m_last_error = 0;
    return true;
}

bool ufile_t::flush() {
    if (!m_handle) {
        m_last_error = EBADF;
        return false;
    }
    
    if (fflush(m_handle) != 0) {
        m_last_error = errno;
        return false;
    }
    
    return true;
}

bool ufile_t::copy(const fs::path& src, const fs::path& dst, size_t buffer_size) {
    if (src.empty() || dst.empty() || buffer_size == 0) {
        errno = EINVAL;
        return false;
    }
    
    // Открываем исходный файл
    FILE* src_file = fopen(src.lexically_normal().c_str(), "rb");
    if (!src_file) {
        return false;
    }
    
    // Открываем целевой файл
    FILE* dst_file = fopen(dst.lexically_normal().c_str(), "wb");
    if (!dst_file) {
        fclose(src_file);
        return false;
    }
    
    // Копируем блоками
    bool success = true;
    char* buffer = new char[buffer_size];
    
    if (!buffer) {
        success = false;
    } else {
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, buffer_size, src_file)) > 0) {
            if (fwrite(buffer, 1, bytes_read, dst_file) != bytes_read) {
                success = false;
                break;
            }
        }
        
        delete[] buffer;
    }
    
    fclose(src_file);
    fclose(dst_file);
    
    return success;
}

bool ufile_t::exists(const fs::path& path) {
    if (path.empty()) {
        return false;
    }
    
    struct stat st;
    auto v = stat(path.c_str(), &st);
    return v == 0 && (st.st_mode & S_IFREG);  // Только файлы
}

// ============================================================================
// intetnal
// ============================================================================

const char* ufile_t::mode_to_string(file_mode mode) {
    switch (mode) {
        case file_mode::read_rb:          return "rb";
        case file_mode::write_wb:         return "wb";
        case file_mode::append_ab:        return "ab";
        case file_mode::read_write_r_b:    return "r+b";
        case file_mode::write_read_w_b:    return "w+b";
        case file_mode::append_read_a_b:   return "a+b";
        default:                            return "rb";
    }
}

} // namespace ufo