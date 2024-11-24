#include <optional>
#include <string>
#include <unordered_map>

using std::string;
using std::optional;
using std::unordered_map;

struct document_index_entry {
    size_t key_size;
    size_t value_offset;
};

struct offsets_index_entry {
    size_t key_in_index_offset;
    size_t value_in_storage_offset;
};

struct document_entry {
    size_t size;
    const char* data;
};

/*
storage file layout:
(size of data in bytes, data)

index file layout:
(key size in bytes, offset of value in storage file, key)
*/
class document_like {
    private:
        int storage_fd;
        int index_fd;
        unordered_map<string, offsets_index_entry> offsets_index;
        size_t storage_file_size;
        size_t index_file_size;

        void init_index();

    public:
        document_like(const string& folder);

        ~document_like();

        void add(const std::string& key, const document_entry& value);

        optional<document_entry> get(const string& key) const noexcept;
};