// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SERIALIZE_H
#define BITCOIN_SERIALIZE_H

#include <compat/endian.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ios>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string.h>
#include <utility>
#include <vector>

#include <prevector.h>
#include <span.h>

/**
 * The maximum size of a serialized object in bytes or number of elements
 * (for eg vectors) when the size is encoded as CompactSize.
 */
static constexpr uint64_t MAX_SIZE = 0x02000000;

/** Maximum amount of memory (in bytes) to allocate at once when deserializing vectors. */
static const unsigned int MAX_VECTOR_ALLOCATE = 5000000;

/**
 * Dummy data type to identify deserializing constructors.
 *
 * By convention, a constructor of a type T with signature
 *
 *   template <typename Stream> T::T(deserialize_type, Stream& s)
 *
 * is a deserializing constructor, which builds the type by
 * deserializing it from s. If T contains const fields, this
 * is likely the only way to do so.
 */
struct deserialize_type {};
constexpr deserialize_type deserialize {};

//! Safely convert odd char pointer types to standard ones.
inline char* CharCast(char* c) { return c; }
inline char* CharCast(unsigned char* c) { return (char*)c; }
inline const char* CharCast(const char* c) { return c; }
inline const char* CharCast(const unsigned char* c) { return (const char*)c; }

/*
 * Lowest-level serialization and conversion.
 * @note Sizes of these types are verified in the tests
 */
template<typename Stream> inline void ser_writedata8(Stream &s, uint8_t obj)
{
    s.write((char*)&obj, 1);
}
template<typename Stream> inline void ser_writedata16(Stream &s, uint16_t obj)
{
    obj = htole16(obj);
    s.write((char*)&obj, 2);
}
template<typename Stream> inline void ser_writedata16be(Stream &s, uint16_t obj)
{
    obj = htobe16(obj);
    s.write((char*)&obj, 2);
}
template<typename Stream> inline void ser_writedata32(Stream &s, uint32_t obj)
{
    obj = htole32(obj);
    s.write((char*)&obj, 4);
}
template<typename Stream> inline void ser_writedata32be(Stream &s, uint32_t obj)
{
    obj = htobe32(obj);
    s.write((char*)&obj, 4);
}
template<typename Stream> inline void ser_writedata64(Stream &s, uint64_t obj)
{
    obj = htole64(obj);
    s.write((char*)&obj, 8);
}
template<typename Stream> inline uint8_t ser_readdata8(Stream &s)
{
    uint8_t obj;
    s.read((char*)&obj, 1);
    return obj;
}
template<typename Stream> inline uint16_t ser_readdata16(Stream &s)
{
    uint16_t obj;
    s.read((char*)&obj, 2);
    return le16toh(obj);
}
template<typename Stream> inline uint16_t ser_readdata16be(Stream &s)
{
    uint16_t obj;
    s.read((char*)&obj, 2);
    return be16toh(obj);
}
template<typename Stream> inline uint32_t ser_readdata32(Stream &s)
{
    uint32_t obj;
    s.read((char*)&obj, 4);
    return le32toh(obj);
}
template<typename Stream> inline uint32_t ser_readdata32be(Stream &s)
{
    uint32_t obj;
    s.read((char*)&obj, 4);
    return be32toh(obj);
}
template<typename Stream> inline uint64_t ser_readdata64(Stream &s)
{
    uint64_t obj;
    s.read((char*)&obj, 8);
    return le64toh(obj);
}


/////////////////////////////////////////////////////////////////
//
// Templates for serializing to anything that looks like a stream,
// i.e. anything that supports .read(char*, size_t) and .write(char*, size_t)
//

class CSizeComputer;

enum
{
    // primary actions
    SER_NETWORK         = (1 << 0),
    SER_DISK            = (1 << 1),
    SER_GETHASH         = (1 << 2),
};

//! Convert the reference base type to X, without changing constness or reference type.
template<typename X> X& ReadWriteAsHelper(X& x) { return x; }
template<typename X> const X& ReadWriteAsHelper(const X& x) { return x; }

#define READWRITE(...) (::SerReadWriteMany(s, ser_action, __VA_ARGS__))
#define READWRITEAS(type, obj) (::SerReadWriteMany(s, ser_action, ReadWriteAsHelper<type>(obj)))
#define SER_READ(obj, code) ::SerRead(s, ser_action, obj, [&](Stream& s, typename std::remove_const<Type>::type& obj) { code; })
#define SER_WRITE(obj, code) ::SerWrite(s, ser_action, obj, [&](Stream& s, const Type& obj) { code; })

/**
 * Implement the Ser and Unser methods needed for implementing a formatter (see Using below).
 *
 * Both Ser and Unser are delegated to a single static method SerializationOps, which is polymorphic
 * in the serialized/deserialized type (allowing it to be const when serializing, and non-const when
 * deserializing).
 *
 * Example use:
 *   struct FooFormatter {
 *     FORMATTER_METHODS(Class, obj) { READWRITE(obj.val1, VARINT(obj.val2)); }
 *   }
 *   would define a class FooFormatter that defines a serialization of Class objects consisting
 *   of serializing its val1 member using the default serialization, and its val2 member using
 *   VARINT serialization. That FooFormatter can then be used in statements like
 *   READWRITE(Using<FooFormatter>(obj.bla)).
 */
#define FORMATTER_METHODS(cls, obj) \
    template<typename Stream> \
    static void Ser(Stream& s, const cls& obj) { SerializationOps(obj, s, CSerActionSerialize()); } \
    template<typename Stream> \
    static void Unser(Stream& s, cls& obj) { SerializationOps(obj, s, CSerActionUnserialize()); } \
    template<typename Stream, typename Type, typename Operation> \
    static inline void SerializationOps(Type& obj, Stream& s, Operation ser_action) \

/**
 * Implement the Serialize and Unserialize methods by delegating to a single templated
 * static method that takes the to-be-(de)serialized object as a parameter. This approach
 * has the advantage that the constness of the object becomes a template parameter, and
 * thus allows a single implementation that sees the object as const for serializing
 * and non-const for deserializing, without casts.
 */
#define SERIALIZE_METHODS(cls, obj)                                                 \
    template<typename Stream>                                                       \
    void Serialize(Stream& s) const                                                 \
    {                                                                               \
        static_assert(std::is_same<const cls&, decltype(*this)>::value, "Serialize type mismatch"); \
        Ser(s, *this);                                                              \
    }                                                                               \
    template<typename Stream>                                                       \
    void Unserialize(Stream& s)                                                     \
    {                                                                               \
        static_assert(std::is_same<cls&, decltype(*this)>::value, "Unserialize type mismatch"); \
        Unser(s, *this);                                                            \
    }                                                                               \
    FORMATTER_METHODS(cls, obj)

#ifndef CHAR_EQUALS_INT8
template<typename Stream> inline void Serialize(Stream& s, char a    ) { ser_writedata8(s, a); } // TODO Get rid of bare char
#endif
template<typename Stream> inline void Serialize(Stream& s, int8_t a  ) { ser_writedata8(s, a); }
template<typename Stream> inline void Serialize(Stream& s, uint8_t a ) { ser_writedata8(s, a); }
template<typename Stream> inline void Serialize(Stream& s, int16_t a ) { ser_writedata16(s, a); }
template<typename Stream> inline void Serialize(Stream& s, uint16_t a) { ser_writedata16(s, a); }
template<typename Stream> inline void Serialize(Stream& s, int32_t a ) { ser_writedata32(s, a); }
template<typename Stream> inline void Serialize(Stream& s, uint32_t a) { ser_writedata32(s, a); }
template<typename Stream> inline void Serialize(Stream& s, int64_t a ) { ser_writedata64(s, a); }
template<typename Stream> inline void Serialize(Stream& s, uint64_t a) { ser_writedata64(s, a); }
template<typename Stream, int N> inline void Serialize(Stream& s, const char (&a)[N]) { s.write(a, N); }
template<typename Stream, int N> inline void Serialize(Stream& s, const unsigned char (&a)[N]) { s.write(CharCast(a), N); }
template<typename Stream> inline void Serialize(Stream& s, const Span<const unsigned char>& span) { s.write(CharCast(span.data()), span.size()); }
template<typename Stream> inline void Serialize(Stream& s, const Span<unsigned char>& span) { s.write(CharCast(span.data()), span.size()); }

#ifndef CHAR_EQUALS_INT8
template<typename Stream> inline void Unserialize(Stream& s, char& a    ) { a = ser_readdata8(s); } // TODO Get rid of bare char
#endif
template<typename Stream> inline void Unserialize(Stream& s, int8_t& a  ) { a = ser_readdata8(s); }
template<typename Stream> inline void Unserialize(Stream& s, uint8_t& a ) { a = ser_readdata8(s); }
template<typename Stream> inline void Unserialize(Stream& s, int16_t& a ) { a = ser_readdata16(s); }
template<typename Stream> inline void Unserialize(Stream& s, uint16_t& a) { a = ser_readdata16(s); }
template<typename Stream> inline void Unserialize(Stream& s, int32_t& a ) { a = ser_readdata32(s); }
template<typename Stream> inline void Unserialize(Stream& s, uint32_t& a) { a = ser_readdata32(s); }
template<typename Stream> inline void Unserialize(Stream& s, int64_t& a ) { a = ser_readdata64(s); }
template<typename Stream> inline void Unserialize(Stream& s, uint64_t& a) { a = ser_readdata64(s); }
template<typename Stream, int N> inline void Unserialize(Stream& s, char (&a)[N]) { s.read(a, N); }
template<typename Stream, int N> inline void Unserialize(Stream& s, unsigned char (&a)[N]) { s.read(CharCast(a), N); }
template<typename Stream> inline void Unserialize(Stream& s, Span<unsigned char>& span) { s.read(CharCast(span.data()), span.size()); }

template <typename Stream> inline void Serialize(Stream& s, bool a) { uint8_t f = a; ser_writedata8(s, f); }
template <typename Stream> inline void Unserialize(Stream& s, bool& a) { uint8_t f = ser_readdata8(s); a = f; }


/**
 * Compact Size
 * size <  253        -- 1 byte
 * size <= USHRT_MAX  -- 3 bytes  (253 + 2 bytes)
 * size <= UINT_MAX   -- 5 bytes  (254 + 4 bytes)
 * size >  UINT_MAX   -- 9 bytes  (255 + 8 bytes)
 */
inline unsigned int GetSizeOfCompactSize(uint64_t nSize)
{
    if (nSize < 253)             return sizeof(unsigned char);
    else if (nSize <= std::numeric_limits<uint16_t>::max()) return sizeof(unsigned char) + sizeof(uint16_t);
    else if (nSize <= std::numeric_limits<unsigned int>::max())  return sizeof(unsigned char) + sizeof(unsigned int);
    else                         return sizeof(unsigned char) + sizeof(uint64_t);
}

inline void WriteCompactSize(CSizeComputer& os, uint64_t nSize);

template<typename Stream>
void WriteCompactSize(Stream& os, uint64_t nSize)
{
    if (nSize 