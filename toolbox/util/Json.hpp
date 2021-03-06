#pragma once

#include "toolbox/util/Config.hpp"
#include "toolbox/util/RobinHood.hpp"
#include "toolbox/util/InternedStrings.hpp"
#include "toolbox/sys/Error.hpp"

#include <cmath>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <fstream>
#include <initializer_list>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <ostream>
#include <tuple>
#include <utility>
#define SIMDJSON_DLLIMPORTEXPORT TOOLBOX_API

#define SIMDJSON_IMPLEMENTATION_HASWELL 0
#define SIMDJSON_IMPLEMENTATION_WESTMERE 0
#define SIMDJSON_IMPLEMENTATION_FALLBACK 1

#include "../contrib/json/simdjson.h"

namespace toolbox { 
namespace json {

using Parser = simdjson::dom::parser;
using Element = simdjson::dom::element;
using Object = simdjson::dom::object;
using String = simdjson::padded_string;
using Error = simdjson::simdjson_error;
using Int = std::int64_t;
using UInt = std::uint64_t;
using Bool = bool;
using StringView = std::string_view;
using Document = Element;
using ElementType = simdjson::dom::element_type;

struct Key {
    constexpr Key(const char* data, std::size_t size) : data_(data), size_(size) {}
    constexpr explicit Key(std::string_view val) : data_(val.data()), size_(val.size()) {}
    explicit Key(const char* data) : data_(data), size_(std::strlen(data)) {}
    explicit constexpr operator const char*() const { return data_; } 
    explicit constexpr operator std::string_view() const { return std::string_view(data_, size_); }
    constexpr bool operator==(const char* rhs) const { return data_ == rhs || !std::strcmp(data_, rhs); }
    bool operator==(const Key& rhs) const { return data_ == rhs.data_ || (size_==rhs.size_ && !std::strcmp(data_, rhs.data_)); }
    constexpr std::size_t size() const { return size_; }
    constexpr const char* data() const { return data_;}
private:
    const char* data_;
    std::size_t size_;
};
namespace literals {
    constexpr Key operator "" _jk(const char* data, std::size_t size) {
        return Key(data, size);
    }
}

class MutableDocument;

class MutableElement;

struct Pretty {
    MutableElement &value;    
    Pretty(MutableElement& value)
    :value(value) {}
};

class JsonError : public std::runtime_error {
public:
    JsonError(std::string what) : std::runtime_error(what) {}
    JsonError(std::string what, std::string key) : std::runtime_error("'"+key+"':"+what) {}
};

class MutableElement {
public:
    using element_type = ElementType;
public:
    MutableElement(MutableDocument* document=nullptr, element_type element_type=element_type::NULL_VALUE)
    : element_type_(element_type)
    , document_(document)
    {}
public:
    struct const_iterator {
        const_iterator(const MutableElement*e)
        :e(e) {}
        const_iterator operator++() {
            auto result =  const_iterator(e);
            e = e->next_;
            return result;
        }
        const_iterator operator++(int) { e = e->next_; return *this; }
        const MutableElement& operator*() { return *e; }
        const MutableElement* operator->() { return e; }
        bool operator==(const_iterator rhs) { return e==rhs.e;}
        bool operator!=(const_iterator rhs) { return e != rhs.e; }
        const MutableElement* e;
    };
    struct iterator {
        iterator(MutableElement*e)
        :e(e) {}
        iterator operator++() {
            auto result =  iterator(e);
            e = e->next_;
            return result;
        }
        iterator operator++(int) { e = e->next_; return *this; }
        MutableElement& operator*() { return *e; }
        MutableElement* operator->() { return e; }
        bool operator==(iterator rhs) { return e==rhs.e;}
        bool operator!=(iterator rhs) { return e != rhs.e; }
        MutableElement* e;
    };
public:
    element_type type() const noexcept {
        return element_type_;
    }
    bool is_array() const noexcept { return element_type_ == element_type::ARRAY; }
    bool is_object() const noexcept { return element_type_ == element_type::OBJECT; }
    bool is_string() const noexcept { return element_type_ == element_type::STRING; }
    std::string_view get_string() const {
        assert_type(element_type::STRING);
        return std::string_view(value_.str_, size_);
    }
    bool is_int64() const noexcept { return element_type_ == element_type::INT64; }
    std::int64_t get_int64() const {
        assert_type(element_type::INT64);
        return value_.int_;
    }
    bool is_uint64() const noexcept { return element_type_ == element_type::UINT64; }
    std::uint64_t get_uint64() const  {
        assert_type(element_type::UINT64);
        return value_.int_;
    }    
    bool is_double() const noexcept { return element_type_ == element_type::DOUBLE; }
    double get_double() const {
        assert_type(element_type::DOUBLE);
        return value_.dbl_;
    }    
    bool is_bool() const noexcept { return element_type_ == element_type::BOOL; }
    double get_bool() const {
        assert_type(element_type::BOOL);
        return value_.bool_;
    }        
    bool is_null() const noexcept { return element_type_ == element_type::NULL_VALUE; }

    std::string to_string() const {
        switch(element_type_) {
            case element_type::STRING:
                return std::string(value_.str_, size_);
            case element_type::INT64: 
                return std::to_string(value_.int_);
            case element_type::UINT64: 
                return std::to_string((std::uint64_t)value_.int_);
            case element_type::BOOL:
                return std::to_string(value_.bool_);
            default:
                return "";
        }
    }

    MutableElement& at(std::string_view key);
    MutableElement& at(std::size_t index);
    template<typename KeyT>
    MutableElement& operator[](KeyT key) { return at(key); }
    template<typename KeyT>
    const MutableElement& operator[](KeyT key) const { return const_cast<MutableElement*>(this)->at(key); }
    
    bool operator==(const MutableElement & rhs) const {
        if(type() != rhs.type())
            return false;
        switch(type()) {
            case ElementType::BOOL: case ElementType::DOUBLE: case ElementType::INT64: case ElementType::UINT64: return value_.int_ == rhs.value_.int_;
            case ElementType::NULL_VALUE: return rhs.is_null();
            case ElementType::STRING:  {
                auto this_str = get_string();
                auto rhs_str = rhs.get_string();
                return this_str == rhs_str;
            }
            case ElementType::ARRAY: {
                if(size()!=rhs.size())
                    return false;
                std::size_t i=0;
                for(auto e: *this) {
                    if(e!=rhs[i++])
                        return false;
                }
                return true;
            }
            case ElementType::OBJECT: {
                if(size()!=rhs.size())
                    return false;
                for(auto e: *this) {
                    if(rhs.find(e.key()) == nullptr || e!=rhs[e.key()])
                        return false;
                }
                return true;                
            }
        }
        return false;
    }
    bool operator!=(const MutableElement & rhs) const {
        return !operator==(rhs);
    }
    void clear();
    iterator find(std::string_view key);
    const_iterator find(std::string_view key) const;
    iterator find(std::size_t key) const;
    template<typename T>
    const_iterator find_value(const T& val) const {
        MutableElement needle(val);
        if(element_type_==element_type::ARRAY) {
            for(auto &e: *this) {
                if(e==needle)
                    return const_iterator(&e);
            }
        }
        return nullptr;
    }
    iterator erase(iterator it);
    iterator erase(std::string_view key) {
        return erase(find(key));
    }
    iterator erase(std::size_t key) {
        return erase(find(key));
    }
    iterator erase(std::size_t l, std::size_t r) {
        auto it = find(l);
        std::size_t n = r - l;
        while(n-->0 && it!=end()) {
            it = erase(it);
        }
        return it;
    }
    iterator insert_back(const MutableElement &e);
    iterator insert_after(iterator it, const MutableElement &e);
    MutableElement& back();
    void push_back(const MutableElement &e) {
        if(is_array())
            insert_back(e);
        else {
            // FIXME:HACK: if someone does push_back, we drop old value and we're array now
            element_type_ = element_type::ARRAY;
            value_.child_ = nullptr;
            insert_back(e);
        }
    }

    std::size_t size() const { 
        switch(element_type_) {
            case element_type::ARRAY:
            case element_type::OBJECT:
            case element_type::STRING:
                return size_;
            default:
                return 0;
                //throw Error(simdjson::error_code::INCORRECT_TYPE);
        }
        return size_; 
    }

    void swap(MutableElement& rhs);

    template<typename T>
    bool copy(std::vector<T> &result) const {
        if(element_type_!=element_type::ARRAY)
            return false;
        for(auto e: *this) {
            result.push_back(e.get<T>());
        }
        return true;
    }

    template<typename MapT>
    bool copy(MapT &result) const {
        if(element_type_!=element_type::OBJECT)
            return false;
       /* FIXME CLANG 6 does not compile this: 
        for(auto [k, v]: *this) {
            result[k] = v;
        } */
        for(auto it = begin(); it!=end(); it++) {
            result[it->key()] = *it;
        }
        return true;
    }
    
    const_iterator begin() const {
        if(!is_array() && !is_object())
            return nullptr;
        return value_.child_;
    }
    const_iterator end() const {
        return nullptr;
    }
    iterator begin() {
        if(!is_array() && !is_object())
            return nullptr;
        return value_.child_;
    }
    iterator end() {
        return nullptr;
    }
    operator std::pair<const char*, const MutableElement&>() const {
        return std::make_pair(key_,*this);
    }

    MutableElement(const MutableElement& rhs) {
        *this = rhs;
    }

    MutableElement& operator=(MutableElement&& rhs);

    MutableElement(long long val) {
        element_type_ = element_type::INT64;
        value_.int_ = val;
    }
    MutableElement(unsigned long long val) {
        element_type_ = element_type::UINT64;
        value_.int_ = val;
    }
    MutableElement(long val) {
        element_type_ = element_type::INT64;
        value_.int_ = val;
    } 
    MutableElement(unsigned long val) {
        element_type_ = element_type::UINT64;
        value_.int_ = val;
    }

    MutableElement(int val) {
        element_type_ = element_type::INT64;
        value_.int_ = val;
    } 
    MutableElement(unsigned int val) {
        element_type_ = element_type::UINT64;
        value_.int_ = val;
    }  
    MutableElement(short val) {
        element_type_ = element_type::INT64;
        value_.int_ = val;
    } 
    MutableElement(unsigned short val) {
        element_type_ = element_type::UINT64;
        value_.int_ = val;
    }  
    MutableElement(char val) {
        element_type_ = element_type::INT64;
        value_.int_ = val;
    } 
    MutableElement(unsigned char val) {
        element_type_ = element_type::UINT64;
        value_.int_ = val;
    }  
    MutableElement(double val) {
        element_type_ = element_type::DOUBLE;
        value_.dbl_ = val;
    }  
    MutableElement(float val) {
        element_type_ = element_type::DOUBLE;
        value_.dbl_ = val;
    }
    MutableElement(bool val) {
        element_type_ = element_type::BOOL;
        value_.bool_ = val;
    }

    MutableElement(std::string_view val);

    MutableElement(const char* val) : MutableElement(std::string_view(val)) {}

    MutableElement(std::string val) : MutableElement(std::string_view(val)) {}

    // array
    template<class T>
    MutableElement(std::initializer_list<T> lst) {
        element_type_ = element_type::ARRAY;
        iterator it(&back());

        for(auto& e : lst) {
            it = insert_after(it, e);
        }
    }
    template<class T>
    MutableElement(std::vector<T> lst) {
        element_type_ = element_type::ARRAY;
        iterator it(&back());

        for(auto& e : lst) {
            it = insert_after(it, e);
        }
    }
    MutableElement(std::initializer_list<std::pair<Key, MutableElement>> lst) {
        element_type_ = element_type::OBJECT;
        iterator it(&back());
        for(auto &e : lst) {
            it = insert_after(it, e.second);
            (*it).key_ = static_cast<std::string_view>(e.first).data();
        }
    }
    std::ostream& print(std::ostream& os, int pad=-1, std::size_t ncols=16) const {
        switch(type()) {
            case element_type::BOOL: return os << (value_.bool_ ? "true":"false");
            case element_type::INT64: return os << value_.int_;
            case element_type::UINT64: return os << (std::uint64_t)value_.int_;
            case element_type::STRING: return os << "\""<<value_.str_<<"\"";
            case element_type::DOUBLE: return os << value_.dbl_;
            case element_type::NULL_VALUE: return os << "null";
            case element_type::ARRAY: {
                os << "[";
                bool first = true;
                std::size_t cnt=0;
                for(const auto& e: *this) {
                    if(!first) {
                        os <<",";
                        if(pad>=0 && cnt % ncols == 0)
                            os << std::endl << std::setw(pad) << std::setfill(' ') << "";
                    } else {
                        first = false;
                    }
                    e.print(os, pad+1, ncols);
                    cnt++;                    
                }
                assert(cnt==size());
                os << "]";
                return os;
            }
            case element_type::OBJECT: {
                os << "{";
                if(pad>=0 && size()>1)
                    os << std::endl << std::setw(pad) << std::setfill(' ') << "";
                bool first = true;
                std::size_t cnt = 0;
                for(const auto& e: *this) {
                    if(!first) {
                        os << ",";
                        if(pad>=0 && size()>1)
                            os << std::endl << std::setw(pad) << std::setfill(' ') << "";
                    }else {
                        first = false;                    
                    }
                    os << "\"" << e.key_ <<"\":";
                    e.print(os, pad+1, ncols);
                    cnt++;                    
                }
                assert(cnt==size());
                os << "}";
                return os;
            }
        }
        return os;
    }

    friend std::ostream& operator<<(std::ostream& os, const MutableElement& self) {
        return self.print(os);
    }
    constexpr std::string_view key() const { return key_; }
    void key(std::string_view key) { assert(document_==nullptr); key_ = key.data();}
    
    template<std::size_t I>
    auto get() const {
        if      constexpr(I == 0) return key();
        else if constexpr(I == 1) return *this;
    }

    /// conversion via string serialization
    template<typename T>
    T get() const  {
        return TypeTraits<T>::from_string(to_string());
    }

    template<typename T>
    auto& set(std::string_view key, T val) {
        auto &e = at(key);
        e = val;
        return e;
    }

    template<typename T>
    T value_or(std::string_view key, T dflt) const {
        switch(element_type_)
        {
            case ElementType::OBJECT: {
                auto it = find(key);
                return it==nullptr ? dflt : it->get<T>();
            }
            case ElementType::ARRAY: {
                int index = std::atoi(key.data());
                auto it = find(index);
                return it==iterator(nullptr) ? dflt : it->get<T>();
            }
            case ElementType::NULL_VALUE: return dflt;
            default:
                return dflt;
        }
    }

    template<typename T>
    T value(std::string_view key) const noexcept(false) {
        auto it = find(key);
        if(it==nullptr) {
            std::stringstream ss;
            ss << "'"<<key<<"' not found";
            throw JsonError(ss.str());
        }
        return it->get<T>();
    }
    
    std::string_view strv(std::string_view key) const noexcept(false) {
        return value<std::string_view>(key);
    }
    std::string_view strv(std::string_view key, std::string_view dflt) const noexcept(false) {
        return value_or(key, dflt);
    }
    std::string str(std::string_view key) const noexcept(false) {
        return std::string{strv(key)};
    }
    std::string str(std::string_view key, std::string dflt) const noexcept(false) {
        return value_or(key, dflt);
    }
    uint64_t uint64(std::string_view key) const noexcept(false) {
        return value<uint64_t>(key);
    }
    int64_t int64(std::string_view key) const noexcept(false) {
        return value<int64_t>(key);
    }
    bool lgl(std::string_view key) const noexcept(false) {
        return value<bool>(key);
    }
    bool lgl(std::string_view key, bool dflt) const noexcept(false) {
        return value_or(key, dflt);
    }    
    template<typename ElementT>
    static inline void copy(const ElementT& ve, MutableElement& result);

    template<typename ElementT>
    static inline auto to_object(const ElementT &e) {
        if constexpr(std::is_same_v<ElementT, simdjson::dom::element>) {
            return simdjson::dom::object(e);
        } else {
            return e;
        }
    }
    MutableDocument* document() { return document_;}
private:
    MutableElement& operator=(const MutableElement& rhs) {
        element_type_ = rhs.element_type_;
        value_ = rhs.value_;
        next_ = rhs.next_;
        size_ = rhs.size_;
        key_ = rhs.key_;
        return *this;
    }
private:
    void assert_type(element_type type) const {
        if(element_type_!=type) {
            std::stringstream ss;
            ss<<"'"<<key()<<"': found "<<element_type_<<" expected "<<type;
            throw JsonError(ss.str());
        }
    }
    void assert_is_container() const {
        if(element_type_!=element_type::ARRAY && element_type_!=element_type::OBJECT) {
            std::stringstream ss;
            ss<<"'"<<key()<<"': found "<<element_type_<<" expected "<<element_type::ARRAY<<","<<element_type::OBJECT;
            throw JsonError(ss.str());
        }
    }
    
    element_type element_type_{element_type::NULL_VALUE};
    union {
        std::int64_t int_;
        std::double_t dbl_;
        bool bool_;
        const char* str_;
        MutableElement *child_ = nullptr; // first child
    } value_;
    // next element in the array
    MutableElement *next_ = nullptr;
    const char* key_ = nullptr;
    std::size_t size_ = 0;
    MutableDocument *document_ = nullptr;
    friend class MutableDocument;
};

template<>
inline std::string_view MutableElement::get() const  {
    return get_string();
}
template<>
inline std::string MutableElement::get() const {
    return std::string(get_string());
}
template<>
inline std::uint64_t MutableElement::get() const {
    return get_uint64();
}
template<>
inline std::int64_t MutableElement::get() const {
    return get_int64();
}
template<>
inline bool MutableElement::get() const {
    return get_bool();
}

inline std::string get_file_contents(const char *filename)
{
    std::ifstream in(filename, std::ios::in | std::ios::binary);
    if(in)
    {
        std::string contents;
        in.seekg(0, std::ios::end);
        contents.resize(in.tellg());
        in.seekg(0, std::ios::beg);
        in.read(&contents[0], contents.size());
        in.close();
        return contents;
    }
    std::stringstream ss;
    ss << "get_file_contents "<<filename;
    throw std::system_error{make_sys_error(errno), ss.str()};
}

/// Usecase: create MutableDocument, fill with some config, pass to somecode as const, drop alltogether.
class MutableDocument: public MutableElement {
public:
    MutableDocument(element_type element_type=element_type::NULL_VALUE, std::size_t elements_capacity=256)
    : MutableElement(this, element_type) {
        elements_.reserve(elements_capacity);
    }
    MutableElement* alloc_element(MutableElement::element_type type) {
        assert(this);
        elements_.emplace_back(this, type);
        MutableElement& result = elements_.back();
        return &result;
    }
    std::string_view alloc_string(std::string_view str) {
        return strings_.intern(str);
    }
    MutableDocument(const MutableElement& rhs) {
        MutableElement::copy(rhs, *this);
        //*static_cast<MutableElement*>(this) = rhs;
    }
    MutableDocument& operator=(const MutableElement& rhs) {
        clear();
        MutableElement::copy(rhs, *this);
        return *this;
    }
    MutableDocument(const MutableDocument& rhs) 
    : MutableElement()
    {
        MutableElement::copy(rhs, *this);
        //*static_cast<MutableElement*>(this) = rhs;
    }
    MutableDocument& operator=(const MutableDocument& rhs) {
        clear();
        MutableElement::copy(rhs, *this);
        return *this;
    }
    void clear() {
        MutableElement::clear();
        strings_.clear();
        elements_.clear();
    }
    MutableDocument(MutableDocument&& rhs)
    : MutableElement(this) {
        swap(rhs);
    }
    MutableDocument& operator=(MutableDocument&& rhs) {
        swap(rhs);
        return *this;
    }
    void swap(MutableDocument &rhs) {
        MutableElement::swap(rhs);
        std::swap(elements_, rhs.elements_);
        std::swap(strings_, rhs.strings_);
    }

    void parse_file(std::string_view path) {
        auto buf = get_file_contents(path.data());
        cpp_comments_to_whitespace(buf);
        parse(buf);
    }
    static void cpp_comments_to_whitespace(std::string &buf) {
        bool is_string = false;
        bool is_comment = false;
        for(std::size_t i=0; i<buf.size(); i++) {

            switch(buf[i]) {
                case '/':
                    if(i>0 && buf[i-1]=='/' && !is_string) {
                        buf[i] = buf[i-1] = ' ';
                        is_comment = true;
                    }
                    break;
                case '\n':
                    if(!is_string)
                        is_comment = false;
                    break;
                case '\"':
                    if(!is_comment && !(i>0 && buf[i-1]=='\\'))
                        is_string = !is_string;
                    break;
            }
            if(is_comment)
                buf[i] = ' ';
        }
    }
    void parse(std::string_view buf) {
        Parser parser;
        simdjson::dom::element json =  parser.parse(buf.data(), buf.size());
        MutableElement::copy(json, *this);
    }
private:
    std::vector<MutableElement> elements_;
    toolbox::util::InternedStrings strings_;
};

inline MutableElement& MutableElement::at(std::string_view key)
{
    if(element_type_!=element_type::OBJECT && element_type_!=element_type::NULL_VALUE)
        throw JsonError("object type expected", std::string{key});
    element_type_ = element_type::OBJECT;
    MutableElement* e = value_.child_;
    MutableElement* tail = e;
    while(e!=nullptr) {
        if(e->key() == key)
            return *e;
        tail = e;
        e = e->next_;
    }
    assert(document_!=nullptr);
    e = document_->alloc_element(element_type::NULL_VALUE);
    e->key_ = document_->alloc_string(key).data();
    size_++;
    if(tail) {
        tail->next_ = e;
    } else {
        value_.child_ = e;
    }
    assert(e->document_);
    return *e;
}
inline MutableElement& MutableElement::at(std::size_t index)
{
    if(element_type_!=element_type::ARRAY && element_type_!=element_type::NULL_VALUE)
        throw JsonError("array type expected");

    element_type_ = element_type::ARRAY;

    MutableElement* e = value_.child_;
    std::size_t i = 0;
    MutableElement* tail = e;
    while(e!=nullptr) {
        if(i==index)
            return *e;
        i++;
        tail = e;
        e = e->next_;
    }
    while(i<=index) {
        assert(document_!=nullptr);
        e = document_->alloc_element(element_type::NULL_VALUE);
        size_++;
        if(tail) {
            tail->next_ = e;    // append
            tail = e;        
        } else {
            tail = value_.child_ = e;
        }
        i++;
    }
    return *e;
}
inline MutableElement& MutableElement::back() {
    MutableElement* tail = value_.child_;
    while(tail!=nullptr && tail->next_!=nullptr) {
        tail = tail->next_;
    }
    return *tail;
}

inline MutableElement::iterator MutableElement::insert_after(iterator it, const MutableElement &val) {
    MutableElement *e = const_cast<MutableElement*>(&val);
    if(document_!=nullptr) {
        e = document_->alloc_element(element_type::NULL_VALUE);
        *e = val;
        if(e->element_type_==element_type::STRING) {
            e->value_.str_ = document_->alloc_string({e->value_.str_, e->size_}).data();
        }
    }
    MutableElement *prev = &(*it);
    if(prev==nullptr) {
        // replace head
        value_.child_ = e;
        e->next_ = nullptr;
    } else {
        e->next_ = prev->next_;
        prev->next_ = e;
    }
    size_++;
    return iterator(e);
}
inline MutableElement::iterator MutableElement::find(std::string_view key) {
    assert_type(element_type::OBJECT);
    iterator end(nullptr);
    for(iterator it(value_.child_); it!=end; it++)
        if(it->key() == key)
            return it;
    return iterator(nullptr);
}
inline MutableElement::const_iterator MutableElement::find(std::string_view key) const {
    assert_type(element_type::OBJECT);
    const_iterator end(nullptr);
    for(const_iterator it(value_.child_); it!=end; it++)
        if(it->key() == key)
            return it;
    return const_iterator(nullptr);
}
inline MutableElement::iterator MutableElement::find(std::size_t index) const {
    assert_type(element_type::ARRAY);
    iterator end(nullptr);
    if(index>=size())
        return end;
    std::size_t i = 0;
    for(iterator it(value_.child_); it!=end; it++)
        if(i++ == index)
            return it;
    return iterator(nullptr);
}
inline void MutableElement::clear() {
    size_ = 0;
    value_.child_ = nullptr;    // also changes string to null if any
}
inline MutableElement::iterator MutableElement::erase(iterator it) {
    MutableElement *e = value_.child_;
    if(it.e==nullptr)
        return iterator(nullptr);
    if(it.e==e) {
        value_.child_ = value_.child_->next_;
        size_--;
        return iterator(value_.child_);
    }
    while(e!=nullptr) {
        if(e->next_ == it.e) {
            e->next_ = e->next_->next_;
            size_--;
            return iterator(e->next_);
        }
        e=e->next_;
    }
    return iterator(nullptr);
}
inline MutableElement::iterator MutableElement::insert_back(const MutableElement& val) {
    return insert_after(iterator(&back()), val);
}
inline MutableElement::MutableElement(std::string_view val) {
    element_type_ = element_type::STRING;
    value_.str_ = val.data();
    size_ = val.size();
    // document_==nullptr it means that string is not yet copied into document
}
inline void MutableElement::swap(MutableElement &rhs) {
    std::swap(element_type_, rhs.element_type_);
    std::swap(size_, rhs.size_);
    std::swap(value_, rhs.value_);
}
inline MutableElement& MutableElement::operator=(MutableElement&& rhs) {
    assert(this!=&rhs);
    assert(rhs.document_==nullptr || document_==rhs.document_);
    element_type_ = rhs.element_type_;
    size_ = rhs.size_;
    value_ = rhs.value_;
    if(document_!=nullptr && rhs.document_==nullptr && rhs.element_type_==element_type::STRING) {
        // intern the string value
        std::string_view copy = document_->alloc_string(get_string());
        value_.str_ = copy.data();
    }
    if(rhs.element_type_==element_type::ARRAY || rhs.element_type_==element_type::OBJECT) {
        // we drop old childs if any
        value_.child_ = nullptr;
        size_ = 0;
        MutableElement* e = rhs.value_.child_;
        iterator it(&back());
        while(e!=nullptr) {
            it = insert_after(it, *e);
            e=e->next_;
        }
    }
    return *this;
}



template<typename ElementT>
inline void MutableElement::copy(const ElementT& ve, MutableElement& result) {
    switch(ve.type()) {
        case ElementType::BOOL: result = MutableElement(ve.get_bool()); break;
        case ElementType::INT64: result = MutableElement(ve.get_int64()); break;
        case ElementType::UINT64: result = MutableElement(ve.get_uint64()); break;
        case ElementType::DOUBLE: result = MutableElement(ve.get_double()); break;
        case ElementType::NULL_VALUE: result = MutableElement(); break;
        case ElementType::STRING: result = MutableElement(ve.get_string()); break;
        case ElementType::OBJECT: {
            for(auto [k, v]: MutableElement::to_object(ve)) {
                MutableElement::copy(v, result[k]);
            }
            break;
        }
        case ElementType::ARRAY: {
            std::size_t i=0;
            for(auto v: ve) {
                MutableElement::copy(v, result[i++]);
            }
            break;
        }
        default: throw Error(simdjson::error_code::INCORRECT_TYPE);
    }
}

inline std::ostream& operator<<(std::ostream& os, Pretty pretty) {
    return pretty.value.print(os, 0);
}

using MutableObject = MutableElement;

}}

namespace std {
template<> struct tuple_size<toolbox::json::MutableElement> : std::integral_constant<size_t, 2> { };
template<> struct tuple_element<0,toolbox::json::MutableElement> { using type = std::string_view; };
template<> struct tuple_element<1,toolbox::json::MutableElement> { using type = const toolbox::json::MutableElement&; };
}
