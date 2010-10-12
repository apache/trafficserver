/*      Copyright (c) 2006-2010 Network Geographics.
 *      All rights reserved.
 *      Licensed to the Apache Software Foundation.
 */
/* ------------------------------------------------------------------------ */
# if !defined(ATS_NUMERIC_TYPE_HEADER)
# define ATS_NUMERIC_TYPE_HEADER

# include <limits>
/* ----------------------------------------------------------------------- */
namespace ats {
/* ----------------------------------------------------------------------- */
/** @file

    Create a distince type from a builtin numeric type.

    This template class converts a basic type into a class, so that
    instances of the class act like the basic type in normal use but
    as a distinct type when evaulating overloads. This is very handy
    when one has several distinct value types that map to the same
    basic type. That means we can have overloads based on the type
    even though the underlying basic type is the same. The second
    template argument, X, is used only for distinguishing
    instantiations of the template with the same base type. It doesn't
    have to exist. One can declare an instatiation like

    @code
    typedef numeric_type<int, struct some_random_tag_name> some_random_type;
    @endcode

    It is not necessary to ever mention some_random_tag_name
    again. All we need is the entry in the symbol table.
 */

// Forward declare.
template < typename T, typename X > class numeric_type;

/// @cond NOT_DOCUMENTED
/** Support template for resolving operator ambiguitity.

    Not for client use.

    @internal This resolves the problem when @a T is not @c int.
    In that case, because raw numbers are @c int and the overloading
    rule changes created an amibiguity, gcc won't distinguish between
    class methods and builtins because @c numeric_type has a user
    conversion to @a T. So signature <tt>(numeric_type, T)</tt>
    and <tt>(T, int)</tt> are considered equivalent. This defines
    the @c int operators explicitly. We inherit it so if @a T is
    @c int, these are silently overridden.

    @internal Note that we don't have to provide an actual implementation
    for these operators. Funky, isn't it?
*/
template <
  typename T, ///< Base numeric type.
  typename X ///< Distinguishing tag type.
> class numeric_type_int_operators {
public:
    numeric_type<T,X>& operator += ( int t );
    numeric_type<T,X>& operator -= ( int t );

    // Must have const and non-const versions.
    numeric_type<T,X> operator +  ( int t );
    numeric_type<T,X> operator -  ( int t );
    numeric_type<T,X> operator +  ( int t ) const;
    numeric_type<T,X> operator -  ( int t ) const;
};

template < typename T, typename X > numeric_type<T,X>
operator + ( int t, numeric_type_int_operators<T,X> const& );

template < typename T, typename X > numeric_type<T,X>
operator - ( int t, numeric_type_int_operators<T,X> const& );

/// @endcond

/** Numeric type template.
 */
template <
  typename T, ///< Base numeric type.
  typename X ///< Distinguishing tag type.
> class numeric_type : public numeric_type_int_operators<T,X> {
public:
    typedef T raw_type; //!< Base builtin type.
    typedef numeric_type self; //!< Self reference type.

    using numeric_type_int_operators<T,X>::operator +=;
    using numeric_type_int_operators<T,X>::operator -=;
    using numeric_type_int_operators<T,X>::operator +;
    using numeric_type_int_operators<T,X>::operator -;

    /// Default constructor, uninitialized.
    numeric_type();
    //! Construct from implementation type.
    numeric_type(
      raw_type const t ///< Initialized value.
    );
    //! Copy constructor.
    numeric_type(
      self const& that ///< Source instance.
    );

    //! Assignment from implementation type.
    numeric_type & operator = (raw_type const t);
    //! Self assignment.
    numeric_type & operator = (self const& that);

    /// User conversion to implementation type.
    /// @internal If we have just a single const method conversion to a copy
    /// of the @c raw_type then the stream operators don't work. Only a CR
    /// conversion operator satisifies the argument matching.
    operator raw_type const& () const { return _t; }
    /// User conversion to implementation type.
    operator raw_type& () { return _t; }
    /// Explicit conversion to host type
    raw_type raw() const { return _t; }

    // User conversions to raw type provide the standard comparison operators.
    self& operator += ( self const& that );
    self& operator -= ( self const& that );

    self& operator += ( raw_type t );
    self& operator -= ( raw_type t );

    self operator +  ( self const& that );
    self operator -  ( self const& that );

    self operator +  ( raw_type t );
    self operator -  ( raw_type t );

    self& operator ++();
    self operator ++(int);
    self& operator --();
    self operator --(int);

private:
    raw_type   _t;
};

// Method definitions.
template < typename T, typename X > numeric_type<T,X>::numeric_type() { }
template < typename T, typename X > numeric_type<T,X>::numeric_type(raw_type const t) : _t(t) { }
template < typename T, typename X > numeric_type<T,X>::numeric_type(self const& that) : _t(that._t) { }
template < typename T, typename X > numeric_type<T,X>& numeric_type<T,X>::operator = (raw_type const t) { _t = t; return *this; }
template < typename T, typename X > numeric_type<T,X>& numeric_type<T,X>::operator = (self const& that) { _t = that._t; return *this; }

template < typename T, typename X > numeric_type<T,X>& numeric_type<T,X>::operator += ( self const& that ) { _t += that._t; return *this; }
template < typename T, typename X > numeric_type<T,X>& numeric_type<T,X>::operator -= ( self const& that ) { _t -= that._t; return *this; }
template < typename T, typename X > numeric_type<T,X> numeric_type<T,X>::operator +  ( self const& that ) { return self(_t + that._t); }
template < typename T, typename X > numeric_type<T,X> numeric_type<T,X>::operator -  ( self const& that ) { return self(_t - that._t); }

template < typename T, typename X > numeric_type<T,X>& numeric_type<T,X>::operator += ( raw_type t ) { _t += t; return *this; }
template < typename T, typename X > numeric_type<T,X>& numeric_type<T,X>::operator -= ( raw_type t ) { _t -= t; return *this; }
template < typename T, typename X > numeric_type<T,X> numeric_type<T,X>::operator +  ( raw_type t ) { return self(_t + t); }
template < typename T, typename X > numeric_type<T,X> numeric_type<T,X>::operator -  ( raw_type t ) { return self(_t - t); }

template < typename T, typename X > numeric_type<T,X>& numeric_type<T,X>::operator ++() { ++_t; return *this; }
template < typename T, typename X > numeric_type<T,X>& numeric_type<T,X>::operator --() { --_t; return *this; }
template < typename T, typename X > numeric_type<T,X> numeric_type<T,X>::operator ++(int) { self tmp(*this); ++_t; return tmp; }
template < typename T, typename X > numeric_type<T,X> numeric_type<T,X>::operator --(int) { self tmp(*this); --_t; return tmp; }

template < typename T, typename X > numeric_type<T,X> operator +  ( T const& lhs, numeric_type<T,X> const& rhs ) { return rhs + lhs; }
template < typename T, typename X > numeric_type<T,X> operator -  ( T const& lhs, numeric_type<T,X> const& rhs ) { return numeric_type<T,X>(lhs - rhs.raw()); }

/* ----------------------------------------------------------------------- */
} /* end namespace ngeo */
/* ----------------------------------------------------------------------- */
# endif // ATS_NUMERIC_TYPE_HEADER
