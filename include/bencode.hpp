#ifndef INC_BENCODE_HPP
#define INC_BENCODE_HPP

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Try to use N4480's any and string_view classes, or fall back to Boost's.
#if !defined(BENCODE_NO_STDLIB_EXTS) && defined(__has_include)
#  if __has_include(<experimental/any>)
#    include <experimental/any>
#    define BENCODE_ANY_NS std::experimental
#  else
#    include <boost/any.hpp>
#    define BENCODE_ANY_NS boost
#  endif
#  if __has_include(<experimental/string_view>)
#    include <experimental/string_view>
#    define BENCODE_STRING_VIEW std::experimental::string_view
#  else
#    include <boost/utility/string_ref.hpp>
#    define BENCODE_STRING_VIEW boost::string_ref
#  endif
#else
#  include <boost/any.hpp>
#  include <boost/utility/string_ref.hpp>
#  define BENCODE_ANY_NS boost
#  define BENCODE_STRING_VIEW boost::string_ref
#endif

namespace bencode {

  using string = std::string;
  using integer = long long;
  using list = std::vector<BENCODE_ANY_NS::any>;
  using dict = std::map<std::string, BENCODE_ANY_NS::any>;

  using string_view = BENCODE_STRING_VIEW;
  using dict_view = std::map<BENCODE_STRING_VIEW, BENCODE_ANY_NS::any>;

  enum eof_behavior {
    check_eof,
    no_check_eof
  };

  template<typename T>
  BENCODE_ANY_NS::any decode(T &begin, T end);

  namespace detail {
    template<bool View, typename T>
    BENCODE_ANY_NS::any decode_data(T &begin, T end);

    template<typename T>
    integer decode_int_real(T &begin, T end) {
      assert(*begin == 'i');
      ++begin;

      bool positive = true;
      if(*begin == '-') {
        positive = false;
        ++begin;
      }

      // TODO: handle overflow
      integer value = 0;
      for(; begin != end && std::isdigit(*begin); ++begin)
        value = value * 10 + *begin - '0';
      if(begin == end)
        throw std::invalid_argument("unexpected end of string");
      if(*begin != 'e')
        throw std::invalid_argument("expected 'e'");

      ++begin;
      return positive ? value : -value;
    }

    template<bool View, typename T>
    inline integer decode_int(T &begin, T end) {
      return decode_int_real(begin, end);
    }

    template<bool View>
    class str_reader {
    public:
      template<typename Iter, typename Size>
      inline string operator ()(Iter &begin, Iter end, Size len) {
        return call(
          begin, end, len,
          typename std::iterator_traits<Iter>::iterator_category()
        );
      }
    private:
      template<typename Iter, typename Size>
      string call(Iter &begin, Iter end, Size len, std::forward_iterator_tag) {
        if(std::distance(begin, end) < static_cast<std::ptrdiff_t>(len))
          throw std::invalid_argument("unexpected end of string");

        std::string value(len, 0);
        std::copy_n(begin, len, value.begin());
        std::advance(begin, len);
        return value;
      }

      template<typename Iter, typename Size>
      string call(Iter &begin, Iter end, Size len, std::input_iterator_tag) {
        std::string value(len, 0);
        for(Size i = 0; i < len; i++) {
          if(begin == end)
            throw std::invalid_argument("unexpected end of string");
          value[i] = *begin;
          ++begin;
        }
        return value;
      }
    };

    template<>
    class str_reader<true> {
    public:
      template<typename Iter, typename Size>
      string_view operator ()(Iter &begin, Iter end, Size len) {
        if(std::distance(begin, end) < static_cast<std::ptrdiff_t>(len))
          throw std::invalid_argument("unexpected end of string");

        string_view value(begin, len);
        std::advance(begin, len);
        return value;
      }
    };

    template<bool View, typename T>
    typename std::conditional<View, string_view, string>::type
    decode_str(T &begin, T end) {
      assert(std::isdigit(*begin));
      std::size_t len = 0;
      for(; begin != end && std::isdigit(*begin); ++begin)
        len = len * 10 + *begin - '0';

      if(begin == end)
        throw std::invalid_argument("unexpected end of string");
      if(*begin != ':')
        throw std::invalid_argument("expected ':'");
      ++begin;

      return str_reader<View>{}(begin, end, len);
    }

    template<bool View, typename T>
    list decode_list(T &begin, T end) {
      assert(*begin == 'l');
      ++begin;

      list value;
      while(begin != end && *begin != 'e') {
        value.push_back(decode_data<View>(begin, end));
      }

      if(begin == end)
        throw std::invalid_argument("unexpected end of string");

      ++begin;
      return value;
    }

    template<bool View, typename T>
    typename std::conditional<View, dict_view, dict>::type
    decode_dict(T &begin, T end) {
      assert(*begin == 'd');
      ++begin;

      typename std::conditional<View, dict_view, dict>::type value;
      while(begin != end && *begin != 'e') {
        if(!std::isdigit(*begin))
          throw std::invalid_argument("expected string token");

        auto k = decode_str<View>(begin, end);
        auto v = decode_data<View>(begin, end);
        auto result = value.emplace(std::move(k), std::move(v));
        if(!result.second) {
          throw std::invalid_argument(
            "duplicated key in dict: " + std::string(result.first->first)
          );
        }
      }

      if(begin == end)
        throw std::invalid_argument("unexpected end of string");

      ++begin;
      return value;
    }

    template<bool View, typename T>
    BENCODE_ANY_NS::any decode_data(T &begin, T end) {
      if(begin == end)
        return BENCODE_ANY_NS::any();

      if(*begin == 'i')
        return decode_int<View>(begin, end);
      else if(*begin == 'l')
        return decode_list<View>(begin, end);
      else if(*begin == 'd')
        return decode_dict<View>(begin, end);
      else if(std::isdigit(*begin))
        return decode_str<View>(begin, end);

      throw std::invalid_argument("unexpected type");
    }

  }

  template<typename T>
  inline BENCODE_ANY_NS::any decode(T &begin, T end) {
    return detail::decode_data<false>(begin, end);
  }

  template<typename T>
  inline BENCODE_ANY_NS::any decode(const T &begin, T end) {
    T b(begin);
    return detail::decode_data<false>(b, end);
  }

  inline BENCODE_ANY_NS::any decode(const BENCODE_STRING_VIEW &s) {
    return decode(s.begin(), s.end());
  }

  inline BENCODE_ANY_NS::any
  decode(std::istream &s, eof_behavior e = check_eof) {
    std::istreambuf_iterator<char> begin(s), end;
    auto result = decode(begin, end);
    // If we hit EOF, update the parent stream.
    if(e == check_eof && begin == end)
      s.setstate(std::ios_base::eofbit);
    return result;
  }

  template<typename T>
  inline BENCODE_ANY_NS::any decode_view(T &begin, T end) {
    return detail::decode_data<true>(begin, end);
  }

  template<typename T>
  inline BENCODE_ANY_NS::any decode_view(const T &begin, T end) {
    T b(begin);
    return detail::decode_data<true>(b, end);
  }

  inline BENCODE_ANY_NS::any decode_view(const BENCODE_STRING_VIEW &s) {
    return decode_view(s.begin(), s.end());
  }

  class list_encoder {
  public:
    inline list_encoder(std::ostream &os) : os(os) {
      os.put('l');
    }

    template<typename T>
    inline list_encoder & add(T &&value);

    inline void end() {
      os.put('e');
    }
  private:
    std::ostream &os;
  };

  class dict_encoder {
  public:
    inline dict_encoder(std::ostream &os) : os(os) {
      os.put('d');
    }

    template<typename T>
    inline dict_encoder & add(const BENCODE_STRING_VIEW &key, T &&value);

    inline void end() {
      os.put('e');
    }
  private:
    std::ostream &os;
  };

  // TODO: make these use unformatted output?
  inline void encode(std::ostream &os, integer value) {
    os << "i" << value << "e";
  }

  inline void encode(std::ostream &os, const BENCODE_STRING_VIEW &value) {
    os << value.size() << ":" << value;
  }

  template<typename T>
  void encode(std::ostream &os, const std::vector<T> &value) {
    list_encoder e(os);
    for(auto &&i : value)
      e.add(i);
    e.end();
  }

  template<typename T>
  void encode(std::ostream &os, const std::map<std::string, T> &value) {
    dict_encoder e(os);
    for(auto &&i : value)
      e.add(i.first, i.second);
    e.end();
  }

#ifdef BENCODE_HAS_STRING_VIEW
  template<typename T>
  void encode(std::ostream &os, const std::map<BENCODE_STRING_VIEW, T> &value) {
    dict_encoder e(os);
    for(auto &&i : value)
      e.add(i.first, i.second);
    e.end();
  }
#endif

  // Overload for `any`, but only if the passed-in type is already `any`. Don't
  // accept an implicit conversion!
  template<typename T>
  auto encode(std::ostream &os, const T &value) ->
  typename std::enable_if<std::is_same<T, BENCODE_ANY_NS::any>::value>::type {
    using BENCODE_ANY_NS::any_cast;
    auto &type = value.type();

    if(type == typeid(integer))
      encode(os, any_cast<integer>(value));
    else if(type == typeid(string))
      encode(os, any_cast<string>(value));
    else if(type == typeid(list))
      encode(os, any_cast<list>(value));
    else if(type == typeid(dict))
      encode(os, any_cast<dict>(value));
#ifdef BENCODE_HAS_STRING_VIEW
    else if(type == typeid(string_view))
      encode(os, any_cast<string_view>(value));
    else if(type == typeid(dict_view))
      encode(os, any_cast<dict_view>(value));
#endif
    else
      throw std::invalid_argument("unexpected type");
  }

  namespace detail {
    inline void encode_list_items(list_encoder &) {}

    template<typename T, typename ...Rest>
    void encode_list_items(list_encoder &enc, T &&t, Rest &&...rest) {
      enc.add(std::forward<T>(t));
      encode_list_items(enc, std::forward<Rest>(rest)...);
    }

    inline void encode_dict_items(dict_encoder &) {}

    template<typename Key, typename Value, typename ...Rest>
    void encode_dict_items(dict_encoder &enc, Key &&key, Value &&value,
                           Rest &&...rest) {
      enc.add(std::forward<Key>(key), std::forward<Value>(value));
      encode_dict_items(enc, std::forward<Rest>(rest)...);
    }
  }

  template<typename ...T>
  void encode_list(std::ostream &os, T &&...t) {
    list_encoder enc(os);
    detail::encode_list_items(enc, std::forward<T>(t)...);
    enc.end();
  }

  template<typename ...T>
  void encode_dict(std::ostream &os, T &&...t) {
    dict_encoder enc(os);
    detail::encode_dict_items(enc, std::forward<T>(t)...);
    enc.end();
  }

  template<typename T>
  inline list_encoder & list_encoder::add(T &&value) {
    encode(os, std::forward<T>(value));
    return *this;
  }

  template<typename T>
  inline dict_encoder &
  dict_encoder::add(const BENCODE_STRING_VIEW &key, T &&value) {
    encode(os, key);
    encode(os, std::forward<T>(value));
    return *this;
  }

  template<typename T>
  std::string encode(T &&t) {
    std::stringstream ss;
    encode(ss, std::forward<T>(t));
    return ss.str();
  }

}

#endif
