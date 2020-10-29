#pragma once

#include "toolbox/contrib/xml/pugiconfig.hpp"
#include "toolbox/contrib/xml/pugixml.hpp"

namespace toolbox {
inline namespace xml {

using Document = pugi::xml_document;
using MutableDocument = Document;

class XmlError : public std::runtime_error {
public:
    XmlError(std::string what) : std::runtime_error(what) {}
    XmlError(std::string what, std::string key) : std::runtime_error("'"+key+"':"+what) {}
};

class Parser {
public:
    MutableDocument parse_file(std::string_view path) {
        MutableDocument doc;
        auto result = doc.load_file(path.data());
        if(!result)
            throw XmlError(result.description());
        return doc;
    }
};

} // ns xml
} // ns toolbox