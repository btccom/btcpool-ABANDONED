//          Matthew Avery Coder 2012 - 2013.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef UTILITIES_JS
#define UTILITIES_JS

#include <string>
#include <vector>
#include <memory>
#include <cstdlib>
#include <cctype>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <array>
#include <stdexcept>
#include "Common.h"

namespace Utilities {
namespace JS {

enum class type { Null, Obj, Array, Str, Int, Real, Bool, Undefined };

class Node{
  public:


    //returns the end of the string, or the end iterator
    static const char* parse_string(const char* begin, const char* end) {
      //stop when there's an escaped character or when the string ends
      static std::array<char,2> string_stop= {'"','\\'};
      const char* i = begin;
      while( (i = std::find_first_of(i,end,string_stop.begin(),string_stop.end())) != end) {
        if(*i == '\\') {
          ++i;
          if(i == end){ return end;} 
          ++i;
        } else {
          return i;
        }
      }
      return end;
    }
    
    //parses a number, including floats
    static const char* parse_float(const char* begin, const char* end) {
      const char* i = begin;
      if(*i == '-') { ++i; }
      if(i == end) return end;
      while(isdigit(*i)) { ++i; if(i == end) return end;}

      if(*i == '.') { 
        ++i;
        while(isdigit(*i)) { ++i; if(i == end) return end;  }
      }

      if(*i == 'e' || *i == 'E') {
        ++i;
        if(*i == '+' || *i == '-') { ++i; }
        while(isdigit(*i)) { ++i; if(i == end) return end; }
      }
      return i;
    }

    static bool parse(const char* begin, const char* end, Node &root) {
      //stop on [n]ull,[t]rue,[f]alse,["]string,number,array, or object
      static std::array<char,19> stop = {'n','"','0','1','2','3','4','5','6','7','8','9','-','t','f','[',']','{','}'};
      Node* parent = nullptr;
      Node node;

      const char* i = begin;
      while( (i = std::find_first_of(i,end,stop.begin(),stop.end())) != end) {
       

        if ( *i == '}' || *i == ']' ) {
          if( parent == nullptr || ( parent->type_ != ( (*i == '}') ? JS::type::Obj : JS::type::Array ) )) {
            root.reset_soft();
            return false;
          } else {
            if(parent->parent_ == nullptr) {
              return true;
            }
            parent = parent->parent_;
          }
          ++i;
          continue;
        }

        node.reset();
        
        if (parent != nullptr && parent->type_ == JS::type::Obj) {
          //parse the key and continue
          node.key_start_ = ++i;
          if( (i = Node::parse_string(i,end)) == end) {
            root.reset_soft();
            return false;
          }

          node.key_end_ = i;
          ++i;
          if( (i = std::find_first_of(i,end,stop.begin(),stop.end())) == end ) {
            root.reset_soft();
            return false;
          }
        }


        if ( *i == '{' || *i == '[' ) {
          node.children_ = std::make_shared<std::vector<Node> >();
          node.sorted_ = true;
          node.type_ = (*i == '{') ? JS::type::Obj : JS::type::Array;
          ++i;
        } else if ( *i == 'n' ) {
          node.type_ = JS::type::Null;
          ++i;
        } else if ( *i == 't' || *i == 'f') {
          node.type_ = JS::type::Bool;
          node.start_ = i;
          ++i;
        } else if ( *i == '"') {
          node.start_ = ++i;
          if( (i = Node::parse_string(i,end)) == end) {
            root.reset_soft();
            return false;
          }
          node.end_ = i;
          node.type_ = JS::type::Str;
          ++i;
        } else {
          node.start_ = i;
          if(*i == '-') { ++i;}
          if(i == end) {
            root.reset_soft();
            return false;

          }
          while(isdigit(*i)) { ++i; }
          auto j = Node::parse_float(i,end);
          if( j == end) {
            root.reset_soft();
            return false;
          } 

          if( i != j) {
            node.type_ = JS::type::Real;
            node.end_ = j;
            i = j;
          } else {
            node.type_ = JS::type::Int;
            node.end_ = i;
          }
        }
        
        if( parent == nullptr) {
          root.set_soft(node);
          if(node.type_ == JS::type::Obj || node.type_ ==JS::type::Array) {
            parent = &root;
          } else {
            return true;
          }
        } else {
          node.parent_ = parent; 
          if(node.has_key() && parent->children_->size() > 0 &&
                 ( node < parent->children_->back() )) {
            parent->sorted_ = false;
          }
          parent->children_->push_back(node);
          if(node.type_ == JS::type::Obj || node.type_ ==JS::type::Array) {
            parent = &parent->children_->back();
          }
        }
      }

      //this code should never be reached in a valid json object
      root.reset_soft();
      return false;
    }

    void sort_objects() {
      if(type_ == JS::type::Obj || type_ == JS::type::Array) {
        if(type_ == JS::type::Obj && sorted_ == false) {
          std::sort(children_->begin(), children_->end());
          sorted_ = true;
        }
        for(auto child:*children_) {
            child.sort_objects();
        }
      }
    }

    std::ostream& print(std::ostream &out) const{
      return Node::print(out,*this);
    }
  
    static std::ostream& print(std::ostream &out,const Node &node) {
      if(node.has_key()) {
        out << '"' << std::string(node.key_start_,node.key_end_) << '"' << ":";
      }

      if(node.type_ == JS::type::Array || node.type_ == JS::type::Obj) {
        out << ((node.type_ == JS::type::Obj) ? "{" : "[");
        auto n = node.children_->cbegin();
        while(n != node.children_->cend()){
            Node::print(out,*n);
            ++n;
          while(n != node.children_->cend()) {
            out << ",";
            Node::print(out,*n);
            ++n;
          }
        }
        out << ((node.type_ == JS::type::Obj) ? "}" : "]");
      } else if(node.type_ == JS::type::Bool) {
        out << (node.boolean() ? "true":"false");
      } else if(node.type_ == JS::type::Null) {
        out << "null";
      } else if(node.type_ == JS::type::Str) {
        out << '"' << std::string(node.start_,node.end_) << '"';
      } else if(node.type_ == JS::type::Int || node.type_ == JS::type::Real){
        out << std::string(node.start_,node.end_);
      } else {
        out << "undefined";
      }

      return out;
    }

    void detach() {
      parent_ = nullptr;
    }

    void reset_soft() {
      //don't change the parent
      children_.reset();
      type_ = JS::type::Undefined;
   }

    void reset() {
      parent_ = nullptr;
      children_.reset();
      type_ = JS::type::Undefined;
   }

    void set_soft(const Node &node) {
      //don't change the parent
      type_ = node.type_;
      key_start_ = node.key_start_;
      key_end_ = node.key_end_;
      start_ = node.start_;
      end_ = node.end_;
      children_ = node.children_;
    }

    Node():type_(JS::type::Undefined),parent_(nullptr) {
    }
    
    JS::type type() const { return type_; }

    const char* start() const { return start_; }
    const char* end() const { return end_; }
    size_t size() const{ return std::distance(start_,end_); }

    const char* key_start() const { return key_start_; }
    const char* key_end() const { return key_end_; }
    size_t key_size() const{ return std::distance(key_start_,key_end_); }
    
    Node* parent() const{ return parent_; }

    std::shared_ptr<std::vector<Node> > children() const{ return children_; }
  
    Node node() {
      return *this;
    }

    bool operator <(const JS::Node& rhs) const{
      return std::lexicographical_compare(
                      key_start_,key_end_
                     ,rhs.key_start(),rhs.key_end());
    }

    Node operator[](const char *val) {
      auto val_end = val + strlen(val);
      if(type_ == JS::type::Obj) {
        if(!sorted_) {
          std::sort(children_->begin(), children_->end());
          sorted_ = true;
        }
        const auto comp = [this,val_end](const Node& node, const char* val) {
              return std::lexicographical_compare(
                              node.key_start_,node.key_end_
                             ,val,val_end);
            };
        auto it = std::lower_bound(children_->begin(),children_->end()
            ,val,comp);
        if(it != children_->end() && it->keys_equal(val,val_end)) {
          Node ret = *it;
          ret.detach();
          return ret;
        }
      }
      return Node();
    }

    bool keys_equal(const Node &rhs) const {
      return std::lexicographical_compare(
                      key_start_,key_end_
                     ,rhs.key_start(),rhs.key_end());
    }

    bool keys_equal(const char * begin, const char *end) const{
      //todo c++14 change this
      if(std::distance(begin,end) != std::distance(key_start_, key_end_)) {
        return false;
      }
      return std::equal( key_start_, key_end_, begin);
    }


    bool has_key() const { return parent_ != nullptr && parent_->type_ == JS::type::Obj; }
    bool sorted() const { return sorted_;}
    int8_t int8() const{ return *start_; }
    uint8_t uint8() const { return *start_; }
    int16_t int16() const { return strtol(start_,nullptr,10); }
    uint16_t uint16() const { return strtoul(start_,nullptr,10); }
    int32_t int32() const { return strtol(start_,nullptr,10); }
    uint32_t uint32() const { return strtoul(start_,nullptr,10); }
    uint32_t uint32_hex() const { return strtoul(start_,nullptr,16); }
    int64_t int64() const { return strtoll(start_,nullptr,10); }
    uint64_t uint64() const { return strtoull(start_,nullptr,10); }
    uint64_t uint64_hex() const { return strtoull(start_,nullptr,16); }
    float real() const { return strtof(start_,nullptr); }
    std::string str() const { return std::string(start_,end_); }
    bool boolean() const { return (*start_ == 't' ) ? true : false; }

    std::vector<Node>& obj() { return *children_; }
    std::vector<Node>& array() { return *children_; }

  private:
    JS::type type_;
    const char* key_start_;
    const char* key_end_;
    const char* start_;
    const char* end_;
    bool sorted_;
    Node* parent_;
    std::shared_ptr<std::vector<Node> > children_;

};

inline std::ostream& operator<<(std::ostream &os, const Node& node) {
  return node.print(os);
}

}
}

typedef Utilities::JS::Node JsonNode;


#endif
