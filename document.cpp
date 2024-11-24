#pragma once

#include <string>
#include <cstdlib>
#include <fcntl.h>
#include <errno.h>
#include <stdexcept>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include "document.h"
#include <iostream>

using std::string;
using std::to_string;
using std::runtime_error;
using std::filesystem::path;
using std::optional;
using std::unordered_map;

static void check_system_call_result(long long result, const string& command) {
    if (result < 0) {
        throw runtime_error(string("Something goes wrong: '") + command + "' returns " + to_string(result) + "; error: " + ::strerror(errno));
    }
}

static bool read_n_bytes(int fd, char* buffer, size_t n_bytes) {
    size_t offset = 0;
    while (offset < n_bytes) {
        ssize_t read_bytes = ::read(fd, buffer + offset, n_bytes - offset);
        if (read_bytes <= 0) {
            return false;
        }
        offset += read_bytes;
    }
    return true;
}

static void write_n_bytes(int fd, const char* buffer, size_t n_bytes) {
    size_t offset = 0;
    while (offset < n_bytes) {
        ssize_t written_bytes = ::write(fd, buffer + offset, n_bytes - offset);
        check_system_call_result(written_bytes, "write");
        offset += written_bytes;
    }
}

void document_like::init_index() {
    size_t key_buffer_size = sizeof(document_index_entry);
    char* key_buffer = new char[key_buffer_size];
    size_t offset = 0;
    check_system_call_result(::lseek64(index_fd, 0, SEEK_SET), "index lseek64(0)");
    while (read_n_bytes(index_fd, key_buffer, sizeof(document_index_entry))) {
        document_index_entry index_entry = *reinterpret_cast<document_index_entry*>(key_buffer);
        std::cerr << "Read index entry key_size=" << index_entry.key_size << " value_offset=" << index_entry.value_offset << std::endl;
        if (key_buffer_size < index_entry.key_size) {
            std::cerr << "Current buffer is not enough to store read key (" << key_buffer_size << " < " << index_entry.key_size << "), reallicating key buffer" << std::endl;
            delete[] key_buffer;
            key_buffer_size = index_entry.key_size;
            key_buffer = new char[key_buffer_size];
        }

        if (!read_n_bytes(index_fd, key_buffer, index_entry.key_size)) {
            throw runtime_error(string("failed to read index file: ") + ::strerror(errno));
        }
        std::cerr << "Read index key " << string(key_buffer, index_entry.key_size) << std::endl;
        offsets_index.emplace(string(key_buffer, index_entry.key_size), offsets_index_entry{offset, index_entry.value_offset});

        offset += sizeof(document_entry) + index_entry.key_size;
    }
}

document_like::document_like(const string& folder) {
    path folder_path(folder);
    std::filesystem::create_directories(folder);

    this->index_fd = ::open((folder_path / "index").c_str(), O_CREAT | O_RDWR, 0777);
    check_system_call_result(this->index_fd, "open index");

    this->storage_fd = ::open((folder_path / "storage").c_str(), O_CREAT | O_RDWR, 0777);

    try {
        check_system_call_result(this->storage_fd, "open storage");
    } catch (...) {
        ::close(index_fd);
        throw;
    }
    
    init_index();

    struct stat64 buf;
    check_system_call_result(fstat64(this->storage_fd, &buf), "stat storage file");
    this->storage_file_size = buf.st_size;

    check_system_call_result(fstat64(this->index_fd, &buf), "stat index file");
    this->index_file_size = buf.st_size;
}

document_like::~document_like() {
    ::close(storage_fd);
    ::close(index_fd);
    offsets_index.~unordered_map();
}

void document_like::add(const std::string& key, const document_entry& value) {
    unordered_map<string, offsets_index_entry>::iterator key_iterator = offsets_index.find(key);
    offsets_index_entry new_entry{index_file_size, storage_file_size};

    check_system_call_result(lseek64(storage_fd, storage_file_size, SEEK_SET), "storage lseek64(end)");
    write_n_bytes(storage_fd, reinterpret_cast<const char*>(&value.size), sizeof(size_t));
    write_n_bytes(storage_fd, value.data, value.size);
    check_system_call_result(::fdatasync(storage_fd), "storage fdatasync");
    storage_file_size += sizeof(size_t) + value.size;

    if (key_iterator != offsets_index.end()) {
        // Key already exists, replace corresponding value offset
        loff_t index_offset = ::lseek64(index_fd, static_cast<loff_t>(key_iterator->second.key_in_index_offset + sizeof(size_t)), SEEK_SET);
        check_system_call_result(index_offset, "index seek");

        write_n_bytes(index_fd, reinterpret_cast<const char*>(&new_entry.value_in_storage_offset), sizeof(size_t));
        check_system_call_result(::fdatasync(index_fd), "index fdatasync");

        key_iterator->second.value_in_storage_offset = new_entry.value_in_storage_offset;
    } else {
        document_index_entry new_document_index_entry{key.size(), new_entry.value_in_storage_offset};
        check_system_call_result(lseek64(index_fd, index_file_size, SEEK_SET), "index lseek64(end)");
        write_n_bytes(index_fd, reinterpret_cast<const char*>(&new_document_index_entry), sizeof(document_index_entry));
        write_n_bytes(index_fd, key.c_str(), key.size());
        check_system_call_result(::fdatasync(index_fd), "index fdatasync");

        index_file_size += sizeof(document_index_entry) + key.size();

        offsets_index.emplace(key, new_entry);
    }
}

optional<document_entry> document_like::get(const string& key) const noexcept {
    unordered_map<string, offsets_index_entry>::const_iterator iterator = offsets_index.find(key);
    if (iterator == offsets_index.end()) {
        return optional<document_entry>();
    }

    size_t entry_size;
    loff_t offset = ::lseek64(this->storage_fd, iterator->second.value_in_storage_offset, SEEK_SET);
    if (offset < 0) {
        return optional<document_entry>();
    }

    if (!read_n_bytes(storage_fd, reinterpret_cast<char*>(&entry_size), sizeof(size_t))) {
        return optional<document_entry>();
    }

    char *data = new char[entry_size];
    if (!read_n_bytes(storage_fd, data, entry_size)) {
        return optional<document_entry>();
    }

    return optional<document_entry>(document_entry{entry_size, data});
}
