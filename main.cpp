#include "document.h"
#include <iostream>
#include <vector>
#include <functional>

using std::cout;
using std::endl;
using std::vector;
using std::function;

std::ostream& operator<<(std::ostream& stream, const vector<int>& array) {
    stream << "[";
    for (size_t i = 0; i < array.size(); i++) {
        if (i > 0) {
            stream << ", ";
        }
        stream << array[i];
    }
    stream << "]";
    return stream;
 }

void write(document_like& doc, const string& key, int value) {
    doc.add(key, document_entry{sizeof(int), reinterpret_cast<const char*>(&value)});
}

void write(document_like& doc, const string& key, const vector<int>& value) {
    doc.add(key, document_entry{sizeof(int) * value.size(), reinterpret_cast<const char*>(value.data())});
}

void write(document_like& doc, const string& key, const string& value) {
    doc.add(key, document_entry{value.size(), value.data()});
}

optional<int> deserialize_int(const document_entry& entry) {
    if (entry.size != sizeof(int)) {
        return optional<int>();
    }
    return optional<int>(*reinterpret_cast<const int*>(entry.data));
}

optional<vector<int>> deserialize_array(const document_entry& entry) {
    if (entry.size % sizeof(int) != 0) {
        return optional<vector<int>>();
    }
    size_t size = entry.size / sizeof(int);
    const int* array = reinterpret_cast<const int*>(entry.data);
    return optional<vector<int>>(vector<int>(array, array + size));
}

optional<string> deserialize_string(const document_entry& entry) {
    return optional<string>(string(string(entry.data, entry.size)));
}

template<typename T>
void check_same_value(const document_like& doc, const string& key, const T& expected, function<optional<T>(const document_entry&)> deserializer) {
    optional<document_entry> entry = doc.get(key);
    if (!entry.has_value()) {
        cout << "\x1b[31mNot found key=" << key << "\x1b[0m" << endl;
        exit(1);
    }
    
    optional<T> value = deserializer(*entry);
    if (!value.has_value()) {
        cout << "\x1b[31mFailed to deserialize value" << "\x1b[0m" << endl;
        exit(1);
    }

    if (expected != *value) {
        cout << "\x1b[31mFound value differs" << endl << "Expected: " << expected << endl << "Found: " << *value << "\x1b[0m" << endl;
        exit(1);
    }
}

void simple_add_int() {
    document_like doc("tmp_simple_add_int");

    write(doc, "test", 42);
    check_same_value<int>(doc, "test", 42, deserialize_int);
}


void read_from_previous_write_with_reload() {
    string key = "test";
    int value = 42;
    {
        document_like doc("tmp_read_from_previous_write_with_reload");
        write(doc, key, value);
    }

    {
        document_like doc("tmp_read_from_previous_write_with_reload");
        check_same_value<int>(doc, key, 42, deserialize_int);
    }
}

void check_all_required_data_types_work() {
    document_like doc("tmp_check_all_required_data_types_work");

    string key = "key1";

    {
        string value = "I am string";
        write(doc, key, value);
        check_same_value<string>(doc, key, value, deserialize_string);
    }

    {
        vector<int> value = {1, 2, 42, -4};
        write(doc, key, value);
        check_same_value<vector<int>>(doc, key, value, deserialize_array);
    }

    {
        int value = 2024;
        write(doc, key, value);
        check_same_value<int>(doc, key, value, deserialize_int);
    }
}

void check_multiple_keys_works() {
    document_like doc("check_multiple_keys_works");

    string key1 = "key1";
    string key2 = "key2";

    string temp_value_key1 = "I am string";
    write(doc, key1, temp_value_key1);

    vector<int> value_key2 = {1, 2, 42, -4};
    write(doc, key2, value_key2);

    int value_key1 = 2024;
    write(doc, key1, value_key1);

    check_same_value<int>(doc, key1, value_key1, deserialize_int);
    check_same_value<vector<int>>(doc, key2, value_key2, deserialize_array);
}

int main(int argc, char** argv) {
    cout << "Running simple_add_int" << endl;
    simple_add_int();
    cout << "\e[32mOK simple_add_int\e[0m" << endl << endl;

    cout << "Running read_from_previous_write_with_reload" << endl;
    read_from_previous_write_with_reload();
    cout << "\e[32mOK read_from_previous_write_with_reload\e[0m" << endl << endl;

    cout << "Running check_all_required_data_types_work" << endl;
    check_all_required_data_types_work();
    cout << "\e[32mOK check_all_required_data_types_work\e[0m" << endl << endl;

    cout << "Running check_multiple_keys_works" << endl;
    check_multiple_keys_works();
    cout << "\e[32mOK check_multiple_keys_works\e[0m" << endl << endl;

    cout << "\e[32mAll tests passed!\e[0m" << endl;

    return 0;
}