// asf_enumvar: Rust-enum-like variant driven by C++26 reflection.
//
//   template <class Enum, class Tuple = void> class asf::enumvar::var;
//
// The library is one group -- the annotation interface (alt/alt_tag)
// plus var and its free interface (tag/get/get_if/visit/match/on) --
// all in the single namespace asf::enumvar (alias asf_enumvar).
//
//   - Tuple = void (default): alternative types come from per-enumerator
//     annotations [[=asf::enumvar::alt<T>]] (alias asf_enumvar). An enumerator
//     without the annotation maps to std::monostate (fieldless variant).
//   - Tuple = std::tuple<Ts...>: alternatives are Ts... in declaration
//     order. If the tuple is shorter than the enum (including std::tuple<>),
//     the remaining enumerators are fieldless (std::monostate); a longer
//     tuple is ill-formed.
//
// Layout: payload at offset 0 of an inline byte storage; the discriminant
// is a real Enum member of the same storage object ([[no_unique_address]]
// where possible). A fieldless var (all alternatives std::monostate) is
// exactly the Enum itself.
//
// Triviality: destructor / copy / move / assignment are trivial exactly
// when every alternative allows it (same rule as std::variant).
//
// This is a runtime facility: construction/destruction dispatch through
// the runtime index, so var is not usable in constant expressions.
#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <meta>
#include <new>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace asf {

// ---------------------------------------------------------------------------
// The library is one group in the single namespace asf::enumvar (alias
// asf_enumvar): the annotation interface (alt/alt_tag below) plus var and
// its free interface (tag/get/get_if/visit/match/on).
//
// Annotation interface: mark an enumerator's payload type.
//   enum class E { a [[=asf_enumvar::alt<Foo>]], b [[=asf_enumvar::alt<Bar>]] };
// For a computed type reflection, construct the tag directly:
//   [[=asf_enumvar::alt_tag{some_consteval_reflection()}]]
// ---------------------------------------------------------------------------
namespace enumvar {
struct alt_tag {
  std::meta::info type;
};
template <class T>
  requires(std::is_object_v<T> && !std::is_array_v<T>)
inline constexpr alt_tag alt{^^T};

// Construct by enumerator value: var<E>{asf::enumvar::tag<E::some>, args...}
template <auto V>
struct tag_t {
  static constexpr auto value = V;
};
template <auto V>
inline constexpr tag_t<V> tag{};

// A fieldless alternative is denoted by std::monostate -- in the Tuple form
// (std::tuple<int, std::monostate, Blob>), as an explicit
// [[=asf_enumvar::alt<std::monostate>]] annotation, or implicitly (no
// annotation / tuple shorter than the enum). std::monostate is the single
// spelling, so "all alternatives are std::monostate" unambiguously means a
// fieldless enum.

template <class Enum, class Tuple = void>
class var;

namespace detail {

template <class>
struct is_var : std::false_type {};
template <class E, class T>
struct is_var<var<E, T>> : std::true_type {};
template <class T>
inline constexpr bool is_var_v = is_var<T>::value;

// Defined after class var; befriended by it. Positional plumbing for the
// free interface (get / get_if / visit).
template <class Enum, class Tuple>
struct var_access;

template <class T>
struct is_std_tuple : std::false_type {};
template <class... Ts>
struct is_std_tuple<std::tuple<Ts...>> : std::true_type {};

// ------------------------- reflection: alternative set ---------------------

template <class Enum>
consteval std::size_t enum_count() {
  return std::meta::enumerators_of(^^Enum).size();
}

// Payload type of one enumerator: its [[=enumvar::alt<T>]], else monostate.
consteval std::meta::info annotated_payload(std::meta::info enumerator) {
  auto anns = std::meta::annotations_of_with_type(enumerator, ^^enumvar::alt_tag);
  if (anns.empty())
    return ^^std::monostate;
  if (anns.size() != 1)
    throw std::meta::exception("asf: duplicate alt annotation on enumerator",
                               enumerator, std::source_location::current());
  auto t = std::meta::extract<enumvar::alt_tag>(anns[0]).type;
  if (!std::meta::is_type(t))
    throw std::meta::exception(
        "asf: enumvar::alt argument must be a type reflection (^^T)", enumerator,
        std::source_location::current());
  return t;
}

template <class Enum>
consteval std::meta::info reflected_alt_tuple() {
  std::vector<std::meta::info> ts;
  for (auto e : std::meta::enumerators_of(^^Enum))
    ts.push_back(annotated_payload(e));
  return std::meta::substitute(^^std::tuple, ts);
}

// Resolve the second template argument into a std::tuple<Ts...> reflection.
//   void            -> per-enumerator annotations
//   std::tuple<Ts..> -> positional payloads; enumerators past the tuple's
//                       length are fieldless (std::monostate). More types
//                       than enumerators is ill-formed.
template <class Enum, class Tuple>
consteval std::meta::info resolve_alt_tuple() {
  constexpr std::size_t n = enum_count<Enum>();
  if (n == 0)
    throw std::meta::exception("asf::enumvar::var: enum has no enumerators", ^^Enum,
                               std::source_location::current());
  if constexpr (std::is_void_v<Tuple>) {
    return reflected_alt_tuple<Enum>();
  } else if constexpr (!is_std_tuple<Tuple>::value) {
    throw std::meta::exception(
        "asf::enumvar::var: second argument must be void or std::tuple<...>", ^^Tuple,
        std::source_location::current());
  } else if constexpr (std::tuple_size_v<Tuple> <= n) {
    std::vector<std::meta::info> ts;
    for (std::size_t i = 0; i < std::tuple_size_v<Tuple>; ++i)
      ts.push_back(std::meta::tuple_element(i, ^^Tuple));
    for (std::size_t i = std::tuple_size_v<Tuple>; i < n; ++i)
      ts.push_back(^^std::monostate);
    return std::meta::substitute(^^std::tuple, ts);
  } else {
    throw std::meta::exception(
        "asf::enumvar::var: tuple has more types than the enum has enumerators",
        ^^Tuple, std::source_location::current());
  }
}

// Enumerators may alias (same value, e.g. `a = 0, b = 0`). Value lookups
// (emplace<V>, get<V>, on<V>, ...) resolve to the first enumerator of that
// value, so aliased enumerators must agree on the alternative type;
// otherwise construction through the alias would silently build the wrong
// alternative. Checked here, in both annotation and tuple modes.
template <class Enum, class Tuple>
consteval std::meta::info resolve_alt_tuple_checked() {
  constexpr std::meta::info t = resolve_alt_tuple<Enum, Tuple>();
  auto es = std::meta::enumerators_of(^^Enum);
  for (std::size_t i = 0; i < es.size(); ++i)
    for (std::size_t j = i + 1; j < es.size(); ++j)
      if (std::meta::extract<Enum>(es[i]) == std::meta::extract<Enum>(es[j]) &&
          std::meta::tuple_element(i, t) != std::meta::tuple_element(j, t))
        throw std::meta::exception(
            "asf::enumvar::var: enumerators with the same value must map to the same "
            "alternative type",
            es[j], std::source_location::current());
  return t;
}

// enumerator value table, declaration order
template <class Enum, std::size_t N>
consteval std::array<Enum, N> tag_table_impl() {
  std::array<Enum, N> out{};
  std::size_t i = 0;
  for (auto e : std::meta::enumerators_of(^^Enum))
    out[i++] = std::meta::extract<Enum>(e);
  return out;
}

template <class Enum>
inline constexpr auto tag_table = tag_table_impl<Enum, enum_count<Enum>()>();

// npos when v is not an enumerator value
template <class Enum>
constexpr std::size_t index_of_enum(Enum v) {
  for (std::size_t i = 0; i < tag_table<Enum>.size(); ++i)
    if (tag_table<Enum>[i] == v)
      return i;
  return ~std::size_t{0};
}

// true when the enumerators are exactly 0..N-1 in declaration order
template <class Enum>
inline constexpr bool enum_is_dense_v = [] {
  std::size_t i = 0;
  for (Enum e : tag_table<Enum>)
    if (static_cast<std::underlying_type_t<Enum>>(e) !=
        static_cast<std::underlying_type_t<Enum>>(i++))
      return false;
  return true;
}();

// Runtime value -> position (npos when not an enumerator). Dense enums
// convert in O(1); sparse or aliased ones scan the tag table.
template <class Enum>
constexpr std::size_t pos_of_enum(Enum v) {
  if constexpr (enum_is_dense_v<Enum>) {
    using U = std::underlying_type_t<Enum>;
    const U u = static_cast<U>(v);
    if constexpr (std::is_signed_v<U>)
      if (u < 0)
        return ~std::size_t{0};
    return static_cast<std::make_unsigned_t<U>>(u) < tag_table<Enum>.size()
               ? static_cast<std::size_t>(u)
               : ~std::size_t{0};
  } else {
    return index_of_enum<Enum>(v);
  }
}

// Underlying value unused by any enumerator, to mark the valueless state in
// the stored Enum tag. Tries value+-1 of each enumerator; ill-formed when
// the enumerators saturate the underlying range.
template <class Enum>
consteval std::underlying_type_t<Enum> enum_sentinel() {
  using U = std::underlying_type_t<Enum>;
  constexpr auto vals = tag_table<Enum>;
  auto used = [&](U c) {
    for (Enum e : vals)
      if (static_cast<U>(e) == c)
        return true;
    return false;
  };
  for (Enum e : vals) {
    const U c = static_cast<U>(e);
    if (c < std::numeric_limits<U>::max() && !used(static_cast<U>(c + 1)))
      return static_cast<U>(c + 1);
    if (c > std::numeric_limits<U>::min() && !used(static_cast<U>(c - 1)))
      return static_cast<U>(c - 1);
  }
  throw std::meta::exception(
      "asf::enumvar::var: enum value range exhausted; no spare value for the "
      "valueless state",
      ^^Enum, std::source_location::current());
}
template <class Enum>
inline constexpr auto enum_sentinel_v =
    static_cast<Enum>(enum_sentinel<Enum>());

// ------------------------- small type utilities ----------------------------

// position of T in a tuple; occurrences counted for the unique-check
template <class T, class Tuple>
struct type_pos;
template <class T, class... Ts>
struct type_pos<T, std::tuple<Ts...>> {
  static constexpr std::size_t count = (std::is_same_v<T, Ts> + ... + 0);
  static constexpr std::size_t value = [] {
    std::size_t i = 0, found = ~std::size_t{0};
    ((std::is_same_v<T, Ts> ? found = i : found, ++i), ...);
    return found;
  }();
};

// unique alternative constructible from U&& (simplified std::variant rule)
template <class U, class Tuple>
struct ctor_index;
template <class U, class... Ts>
struct ctor_index<U, std::tuple<Ts...>> {
  static constexpr std::size_t count =
      (std::is_constructible_v<Ts, U &&> + ... + 0);
  static constexpr std::size_t value = [] {
    std::size_t i = 0, found = ~std::size_t{0};
    ((std::is_constructible_v<Ts, U &&> ? found = i : found, ++i), ...);
    return found;
  }();
};

// predicate packs over a tuple
template <class Tuple, template <class> class P>
struct tuple_all;
template <template <class> class P, class... Ts>
struct tuple_all<std::tuple<Ts...>, P> : std::bool_constant<(P<Ts>::value && ...)> {};

template <class T>
struct p_eq : std::bool_constant<std::equality_comparable<T>> {};
template <class T>
struct p_3way : std::bool_constant<std::three_way_comparable<T>> {};
template <class T>
struct p_swap : std::bool_constant<std::is_swappable_v<T>> {};
template <class T>
struct p_move : std::bool_constant<std::is_move_constructible_v<T>> {};
template <class T>
struct p_nothrow_swap : std::bool_constant<std::is_nothrow_swappable_v<T>> {};
template <class T>
struct p_nothrow_move
    : std::bool_constant<std::is_nothrow_move_constructible_v<T>> {};

// ------------------------- storage core ------------------------------------
// Unchecked index dispatch: f(std::integral_constant<size_t, I>{}).
// i must be a valid alternative index (< N); all instantiations of f must
// return the same type.
template <std::size_t N, class F, std::size_t... Is>
decltype(auto) index_dispatch_impl(std::size_t i, F&& f,
                                   std::index_sequence<Is...>) {
  using R =
      decltype(std::forward<F>(f)(std::integral_constant<std::size_t, 0>{}));
  static_assert(
      (std::is_same_v<R,
                      decltype(std::forward<F>(f)(
                          std::integral_constant<std::size_t, Is>{}))> &&
       ...),
      "asf: dispatch target must return the same type for every alternative");
  using Fn = R (*)(F&&);
  static constexpr Fn table[] = {
      +[](F&& g) -> R {
        return std::forward<F>(g)(std::integral_constant<std::size_t, Is>{});
      }...};
  return table[i](std::forward<F>(f));
}
template <std::size_t N, class F>
decltype(auto) index_dispatch(std::size_t i, F&& f) {
  return index_dispatch_impl<N>(i, std::forward<F>(f),
                                std::make_index_sequence<N>{});
}

// General storage: payload in a byte buffer at offset 0, the discriminant as
// a real Enum member (position-based interface on top; the valueless state
// is a spare underlying value, see enum_sentinel).
template <class Enum, class... Ts>
struct store_core {
  static constexpr std::size_t count = sizeof...(Ts);
  static constexpr bool is_fieldless = false;

  static constexpr std::size_t payload_bytes = [] {
    std::size_t m = 1;
    ((m = m < sizeof(Ts) ? sizeof(Ts) : m), ...);
    return m;
  }();
  static constexpr std::size_t alignment = [] {
    std::size_t m = 1;
    ((m = m < alignof(Ts) ? alignof(Ts) : m), ...);
    return m;
  }();
  alignas(alignment) unsigned char bytes[payload_bytes];
  [[no_unique_address]] Enum tag_;

  store_core() = default;

  // The runtime word is the Enum itself; positions appear only at the
  // dispatch boundary (pos).
  Enum raw_tag() const { return tag_; }
  void set_raw_tag(Enum v) { tag_ = v; }

  static bool is_valueless_tag(Enum v) {
    return v == detail::enum_sentinel_v<Enum>;
  }
  bool valueless() const { return is_valueless_tag(tag_); }
  void set_valueless() { tag_ = detail::enum_sentinel_v<Enum>; }

  // Position of the stored value (count when valueless).
  // Aliased enumerators resolve to the first position of their value (they
  // share the alternative type, so this is unambiguous).
  std::size_t pos() const {
    const std::size_t p = detail::pos_of_enum<Enum>(tag_);
    return p == ~std::size_t{0} ? count : p;
  }

  template <std::size_t I>
  using alt_t = std::tuple_element_t<I, std::tuple<Ts...>>;

  template <std::size_t I>
  alt_t<I>* ptr() {
    return std::launder(reinterpret_cast<alt_t<I>*>(bytes));
  }
  template <std::size_t I>
  const alt_t<I>* ptr() const {
    return std::launder(reinterpret_cast<const alt_t<I>*>(bytes));
  }

  // Construct alternative I in raw storage; tag published after success.
  template <std::size_t I, class... Args>
  alt_t<I>& construct(Args&&... args) {
    using T = alt_t<I>;
    T* p = ::new (static_cast<void*>(bytes)) T(std::forward<Args>(args)...);
    tag_ = detail::tag_table<Enum>[I];
    return *p;
  }

  template <std::size_t I>
  void destroy_at() {
    std::destroy_at(ptr<I>());
  }
  void destroy(std::size_t i) {
    if (i >= count)
      return;
    s_dispatch(i, [this](auto ic) { this->destroy_at<ic.value>(); });
  }

  template <class F>
  static decltype(auto) s_dispatch(std::size_t i, F&& f) {
    return index_dispatch<count>(i, std::forward<F>(f));
  }
};

// Fieldless storage (every alternative is std::monostate): the payload is an
// empty member overlapping the tag, so sizeof == sizeof(Enum). A fieldless
// var can never become valueless: every monostate operation is noexcept, so
// the valueless paths are unreachable; set_valueless() is a no-op whose
// transient state is always overwritten by the construct<I>() that follows.
template <class Enum, std::size_t N>
struct store_fieldless {
  static constexpr std::size_t count = N;
  static constexpr bool is_fieldless = true;
  static constexpr std::size_t payload_bytes = 1;
  static constexpr std::size_t alignment = 1;

  [[no_unique_address]] std::monostate payload_;
  Enum tag_;

  store_fieldless() = default;

  // The runtime word is the Enum itself.
  Enum raw_tag() const { return tag_; }
  void set_raw_tag(Enum v) { tag_ = v; }

  static bool is_valueless_tag(Enum) { return false; }
  bool valueless() const { return false; }
  void set_valueless() {}  // unreachable, see above

  std::size_t pos() const { return detail::pos_of_enum<Enum>(tag_); }

  template <std::size_t I>
  using alt_t = std::monostate;

  template <std::size_t I>
  std::monostate* ptr() {
    return &payload_;
  }
  template <std::size_t I>
  const std::monostate* ptr() const {
    return &payload_;
  }

  template <std::size_t I, class... Args>
  std::monostate& construct(Args&&... args) {
    std::monostate* p = ::new (static_cast<void*>(&payload_))
        std::monostate(std::forward<Args>(args)...);
    tag_ = detail::tag_table<Enum>[I];
    return *p;
  }

  template <std::size_t I>
  void destroy_at() {
    std::destroy_at(&payload_);
  }
  void destroy(std::size_t i) {
    if (i >= count)
      return;
    s_dispatch(i, [this](auto ic) { this->destroy_at<ic.value>(); });
  }

  template <class F>
  static decltype(auto) s_dispatch(std::size_t i, F&& f) {
    return index_dispatch<count>(i, std::forward<F>(f));
  }
};

// ------------------- special-member layers -------------------
// std::variant parity ([variant.dtor]/[variant.ctor]/[variant.assign]).
// Each special member is, per alternative set:
//   trivial (2): the corresponding trivially-* trait holds for every
//                alternative (assignments additionally require trivial
//                destruction);
//   deleted (0): the operation is unavailable for some alternative;
//   normal  (1): otherwise -- runtime index dispatch.

template <class... Ts>
struct store_traits {
  static constexpr bool dtr = (std::is_destructible_v<Ts> && ...);
  static constexpr bool t_dtr = (std::is_trivially_destructible_v<Ts> && ...);
  static constexpr bool cct = (std::is_copy_constructible_v<Ts> && ...);
  static constexpr bool t_cct =
      (std::is_trivially_copy_constructible_v<Ts> && ...);
  static constexpr bool mct = (std::is_move_constructible_v<Ts> && ...);
  static constexpr bool t_mct =
      (std::is_trivially_move_constructible_v<Ts> && ...);
  static constexpr bool cas = (std::is_copy_assignable_v<Ts> && ...);
  static constexpr bool t_cas = (std::is_trivially_copy_assignable_v<Ts> && ...);
  static constexpr bool mas = (std::is_move_assignable_v<Ts> && ...);
  static constexpr bool t_mas = (std::is_trivially_move_assignable_v<Ts> && ...);

  static constexpr int dtor_st = t_dtr ? 2 : (dtr ? 1 : 0);
  static constexpr int cctor_st = t_cct ? 2 : (cct ? 1 : 0);
  static constexpr int mctor_st = t_mct ? 2 : (mct ? 1 : 0);
  static constexpr int cassign_st =
      (t_dtr && t_cct && t_cas) ? 2 : ((dtr && cct && cas) ? 1 : 0);
  static constexpr int massign_st =
      (t_dtr && t_mct && t_mas) ? 2 : ((dtr && mct && mas) ? 1 : 0);
};

#define ASF_ENUMVAR_SM_DEFAULTED(C)    \
  C() = default;                    \
  C(const C&) = default;            \
  C(C&&) = default;                 \
  C& operator=(const C&) = default; \
  C& operator=(C&&) = default;      \
  ~C() = default

// ---- destructor ----
template <class Enum, int St, class... Ts>
struct store_dtor;
template <class Enum, class... Ts>
struct store_dtor<Enum, 2, Ts...> : store_core<Enum, Ts...> {
  ASF_ENUMVAR_SM_DEFAULTED(store_dtor);
};
template <class Enum, class... Ts>
struct store_dtor<Enum, 1, Ts...> : store_core<Enum, Ts...> {
  store_dtor() = default;
  store_dtor(const store_dtor&) = default;
  store_dtor(store_dtor&&) = default;
  store_dtor& operator=(const store_dtor&) = default;
  store_dtor& operator=(store_dtor&&) = default;
  ~store_dtor() { this->destroy(this->pos()); }
};
template <class Enum, class... Ts>
struct store_dtor<Enum, 0, Ts...> : store_core<Enum, Ts...> {
  store_dtor() = default;
  store_dtor(const store_dtor&) = default;
  store_dtor(store_dtor&&) = default;
  store_dtor& operator=(const store_dtor&) = default;
  store_dtor& operator=(store_dtor&&) = default;
  ~store_dtor() = delete;
};

// ---- move constructor ----
template <class Enum, int St, class... Ts>
struct store_mctor;
template <class Enum, class... Ts>
struct store_mctor<Enum, 2, Ts...> : store_dtor<Enum, store_traits<Ts...>::dtor_st, Ts...> {
  ASF_ENUMVAR_SM_DEFAULTED(store_mctor);
};
template <class Enum, class... Ts>
struct store_mctor<Enum, 1, Ts...> : store_dtor<Enum, store_traits<Ts...>::dtor_st, Ts...> {
  store_mctor() = default;
  store_mctor(const store_mctor&) = default;
  store_mctor(store_mctor&& o) noexcept(
      (std::is_nothrow_move_constructible_v<Ts> && ...)) {
    this->set_valueless();
    if (!o.valueless())
      store_core<Enum, Ts...>::s_dispatch(o.pos(), [this, &o](auto ic) {
        this->template construct<ic.value>(
            std::move(*o.template ptr<ic.value>()));
      });
  }
  store_mctor& operator=(const store_mctor&) = default;
  store_mctor& operator=(store_mctor&&) = default;
  ~store_mctor() = default;
};
template <class Enum, class... Ts>
struct store_mctor<Enum, 0, Ts...> : store_dtor<Enum, store_traits<Ts...>::dtor_st, Ts...> {
  store_mctor() = default;
  store_mctor(const store_mctor&) = default;
  store_mctor(store_mctor&&) = delete;
  store_mctor& operator=(const store_mctor&) = default;
  store_mctor& operator=(store_mctor&&) = default;
  ~store_mctor() = default;
};

// ---- copy constructor ----
template <class Enum, int St, class... Ts>
struct store_cctor;
template <class Enum, class... Ts>
struct store_cctor<Enum, 2, Ts...> : store_mctor<Enum, store_traits<Ts...>::mctor_st, Ts...> {
  ASF_ENUMVAR_SM_DEFAULTED(store_cctor);
};
template <class Enum, class... Ts>
struct store_cctor<Enum, 1, Ts...> : store_mctor<Enum, store_traits<Ts...>::mctor_st, Ts...> {
  store_cctor() = default;
  store_cctor(const store_cctor& o) noexcept(
      (std::is_nothrow_copy_constructible_v<Ts> && ...)) {
    this->set_valueless();
    if (!o.valueless())
      store_core<Enum, Ts...>::s_dispatch(o.pos(), [this, &o](auto ic) {
        this->template construct<ic.value>(*o.template ptr<ic.value>());
      });
  }
  store_cctor(store_cctor&&) = default;
  store_cctor& operator=(const store_cctor&) = default;
  store_cctor& operator=(store_cctor&&) = default;
  ~store_cctor() = default;
};
template <class Enum, class... Ts>
struct store_cctor<Enum, 0, Ts...> : store_mctor<Enum, store_traits<Ts...>::mctor_st, Ts...> {
  store_cctor() = default;
  store_cctor(const store_cctor&) = delete;
  store_cctor(store_cctor&&) = default;
  store_cctor& operator=(const store_cctor&) = default;
  store_cctor& operator=(store_cctor&&) = default;
  ~store_cctor() = default;
};

// ---- move assignment ----
template <class Enum, int St, class... Ts>
struct store_massign;
template <class Enum, class... Ts>
struct store_massign<Enum, 2, Ts...>
    : store_cctor<Enum, store_traits<Ts...>::cctor_st, Ts...> {
  ASF_ENUMVAR_SM_DEFAULTED(store_massign);
};
template <class Enum, class... Ts>
struct store_massign<Enum, 1, Ts...>
    : store_cctor<Enum, store_traits<Ts...>::cctor_st, Ts...> {
  store_massign() = default;
  store_massign(const store_massign&) = default;
  store_massign(store_massign&&) = default;
  store_massign& operator=(const store_massign&) = default;
  store_massign& operator=(store_massign& o) noexcept(
      ((std::is_nothrow_move_constructible_v<Ts> &&
        std::is_nothrow_move_assignable_v<Ts>) &&
       ...)) {
    if (this == &o)
      return *this;
    const Enum ti = this->raw_tag(), to = o.raw_tag();
    if (ti == to) {
      if (!this->valueless())
        store_core<Enum, Ts...>::s_dispatch(this->pos(), [this, &o](auto ic) {
          *this->template ptr<ic.value>() =
              std::move(*o.template ptr<ic.value>());
        });
    } else {
      this->destroy(this->pos());
      this->set_valueless();
      if (!o.valueless())
        store_core<Enum, Ts...>::s_dispatch(o.pos(), [this, &o](auto ic) {
          this->template construct<ic.value>(
              std::move(*o.template ptr<ic.value>()));
        });
    }
    return *this;
  }
  ~store_massign() = default;
};
template <class Enum, class... Ts>
struct store_massign<Enum, 0, Ts...>
    : store_cctor<Enum, store_traits<Ts...>::cctor_st, Ts...> {
  store_massign() = default;
  store_massign(const store_massign&) = default;
  store_massign(store_massign&&) = default;
  store_massign& operator=(const store_massign&) = default;
  store_massign& operator=(store_massign&&) = delete;
  ~store_massign() = default;
};

// ---- copy assignment ----
template <class Enum, int St, class... Ts>
struct store_cassign;
template <class Enum, class... Ts>
struct store_cassign<Enum, 2, Ts...>
    : store_massign<Enum, store_traits<Ts...>::massign_st, Ts...> {
  ASF_ENUMVAR_SM_DEFAULTED(store_cassign);
};
template <class Enum, class... Ts>
struct store_cassign<Enum, 1, Ts...>
    : store_massign<Enum, store_traits<Ts...>::massign_st, Ts...> {
  store_cassign() = default;
  store_cassign(const store_cassign&) = default;
  store_cassign(store_cassign&&) = default;
  store_cassign& operator=(const store_cassign& o) noexcept(
      ((std::is_nothrow_copy_constructible_v<Ts> &&
        std::is_nothrow_copy_assignable_v<Ts>) &&
       ...)) {
    if (this == &o)
      return *this;
    const Enum ti = this->raw_tag(), to = o.raw_tag();
    if (ti == to) {
      if (!this->valueless())
        store_core<Enum, Ts...>::s_dispatch(this->pos(), [this, &o](auto ic) {
          *this->template ptr<ic.value>() = *o.template ptr<ic.value>();
        });
    } else {
      this->destroy(this->pos());
      this->set_valueless();
      if (!o.valueless())
        store_core<Enum, Ts...>::s_dispatch(o.pos(), [this, &o](auto ic) {
          this->template construct<ic.value>(*o.template ptr<ic.value>());
        });
    }
    return *this;
  }
  store_cassign& operator=(store_cassign&&) = default;
  ~store_cassign() = default;
};
template <class Enum, class... Ts>
struct store_cassign<Enum, 0, Ts...>
    : store_massign<Enum, store_traits<Ts...>::massign_st, Ts...> {
  store_cassign() = default;
  store_cassign(const store_cassign&) = default;
  store_cassign(store_cassign&&) = default;
  store_cassign& operator=(const store_cassign&) = delete;
  store_cassign& operator=(store_cassign&&) = default;
  ~store_cassign() = default;
};

#undef ASF_ENUMVAR_SM_DEFAULTED

template <class Enum, class... Ts>
using storage_t = store_cassign<Enum, store_traits<Ts...>::cassign_st, Ts...>;

template <class Enum, class Tuple>
struct storage_for;
template <class Enum, class... Ts>
struct storage_for<Enum, std::tuple<Ts...>> {
  // All-std::monostate means a fieldless enum (single spelling: there is no
  // other fieldless marker), so the storage shrinks to the Enum itself.
  using type =
      std::conditional_t<(std::is_same_v<Ts, std::monostate> && ...),
                         store_fieldless<Enum, sizeof...(Ts)>,
                         storage_t<Enum, Ts...>>;
};
template <class Enum, class Tuple>
using storage_for_t = typename storage_for<Enum, Tuple>::type;

}  // namespace detail

// ---------------------------------------------------------------------------
// asf::enumvar::var -- Rust-enum-like variant
// ---------------------------------------------------------------------------
template <class Enum, class Tuple>
class var {
  static_assert(std::is_enum_v<Enum>, "asf::enumvar::var: first argument must be an enum");

 public:
  using enum_type = Enum;
  using alt_tuple = [: detail::resolve_alt_tuple_checked<Enum, Tuple>() :];

 private:
  using storage_type = detail::storage_for_t<Enum, alt_tuple>;
  storage_type storage_;

  static constexpr std::size_t count_v = std::tuple_size_v<alt_tuple>;
  static constexpr std::size_t npos_v = std::variant_npos;

  template <std::size_t I>
  using alt_at = std::tuple_element_t<I, alt_tuple>;

  template <class T>
  static constexpr std::size_t pos_of =
      detail::type_pos<T, alt_tuple>::value;

  template <class, class>
  friend struct detail::var_access;

  // Position of enumerator V; npos_v when V is not an enumerator of Enum.
  template <auto V>
  static constexpr std::size_t enum_pos_v = detail::index_of_enum<Enum>(V);
  // Clamped into range for signatures; validity is static_asserted in bodies.
  template <auto V>
  static constexpr std::size_t enum_pos_c =
      enum_pos_v<V> < count_v ? enum_pos_v<V> : 0;

  // ---- positional plumbing (var_access / raw_dispatch only) ----
  template <std::size_t I>
  alt_at<I>& pos_ref() & {
    return *storage_.template ptr<I>();
  }
  template <std::size_t I>
  const alt_at<I>& pos_ref() const& {
    return *storage_.template ptr<I>();
  }
  template <std::size_t I>
  alt_at<I>&& pos_ref() && {
    return std::move(*storage_.template ptr<I>());
  }
  template <std::size_t I>
  const alt_at<I>&& pos_ref() const&& {
    return std::move(*storage_.template ptr<I>());
  }

  template <std::size_t I, class... Args>
  alt_at<I>& emplace_pos(Args&&... args) {
    storage_.destroy(storage_.pos());
    storage_.set_valueless();
    return storage_.template construct<I>(std::forward<Args>(args)...);
  }

 public:
  // ---------------- constructors ----------------
  var() noexcept(std::is_nothrow_default_constructible_v<alt_at<0>>)
    requires std::is_default_constructible_v<alt_at<0>>
  {
    storage_.set_valueless();
    storage_.template construct<0>();
  }

  var(const var&) = default;
  var(var&&) = default;
  var& operator=(const var&) = default;
  var& operator=(var&&) = default;
  ~var() = default;

  template <class T, class... Args>
    requires(detail::type_pos<T, alt_tuple>::count == 1 &&
             std::is_constructible_v<T, Args && ...>)
  explicit var(std::in_place_type_t<T>, Args&&... args) noexcept(
      std::is_nothrow_constructible_v<T, Args && ...>) {
    storage_.set_valueless();
    storage_.template construct<pos_of<T>>(std::forward<Args>(args)...);
  }

  // Construct by enumerator value: var<E>{asf::enumvar::tag<E::x>, args...}
  template <auto V, class... Args>
    requires std::is_same_v<std::remove_cv_t<decltype(V)>, Enum>
  explicit var(tag_t<V>, Args&&... args) noexcept(
      std::is_nothrow_constructible_v<alt_at<enum_pos_c<V>>, Args && ...>) {
    constexpr std::size_t I = enum_pos_v<V>;
    if constexpr (I == npos_v) {
      static_assert(I != npos_v,
                    "asf::enumvar::var: tag<V>: V is not an enumerator of Enum");
    } else {
      storage_.set_valueless();
      storage_.template construct<I>(std::forward<Args>(args)...);
    }
  }

  // Converting constructor (simplified std::variant rule: exactly one
  // alternative must be constructible from U&&).
  template <class U>
    requires(!std::is_same_v<std::remove_cvref_t<U>, var> &&
             detail::ctor_index<U &&, alt_tuple>::count == 1)
  var(U&& u) noexcept(
      std::is_nothrow_constructible_v<
          alt_at<detail::ctor_index<U &&, alt_tuple>::value>, U &&>) {
    constexpr std::size_t I = detail::ctor_index<U&&, alt_tuple>::value;
    storage_.set_valueless();
    storage_.template construct<I>(std::forward<U>(u));
  }

  // Converting assignment (same simplified rule).
  template <class U>
    requires(!std::is_same_v<std::remove_cvref_t<U>, var> &&
             detail::ctor_index<U &&, alt_tuple>::count == 1 &&
             std::is_assignable_v<
                 alt_at<detail::ctor_index<U &&, alt_tuple>::value> &, U &&>)
  var& operator=(U&& u) {
    constexpr std::size_t I = detail::ctor_index<U&&, alt_tuple>::value;
    if (storage_.raw_tag() == detail::tag_table<Enum>[I]) {
      // Fieldless storage overlaps the payload with the tag: assigning the
      // monostate would clobber the tag (and is semantically void anyway).
      if constexpr (!storage_type::is_fieldless)
        *storage_.template ptr<I>() = std::forward<U>(u);
    } else {
      emplace_pos<I>(std::forward<U>(u));
    }
    return *this;
  }

  // ---------------- observers ----------------
  static constexpr std::size_t size() noexcept { return count_v; }

  // Enumerator of the active alternative: the stored word itself.
  // Precondition: !valueless_by_exception().
  Enum index() const noexcept { return storage_.raw_tag(); }

  bool valueless_by_exception() const noexcept {
    return storage_.valueless();
  }

  // Position of an enumerator value; npos when not an enumerator.
  static consteval std::size_t index_of(Enum v) {
    return detail::index_of_enum<Enum>(v);
  }

  // Unchecked dispatch by enumerator value. Precondition: e is an
  // enumerator of Enum.
  template <class F>
  decltype(auto) raw_dispatch(Enum e, F&& f) const {
    return storage_type::s_dispatch(detail::pos_of_enum<Enum>(e),
                                    std::forward<F>(f));
  }
  // Unchecked dispatch over the current tag.
  // Precondition: !valueless_by_exception().
  template <class F>
  decltype(auto) raw_dispatch(F&& f) const {
    return storage_type::s_dispatch(storage_.pos(), std::forward<F>(f));
  }

  // ---------------- modifiers ----------------
  // emplace by enumerator value: v.emplace<E::x>(args...)
  template <auto V, class... Args>
    requires std::is_same_v<std::remove_cv_t<decltype(V)>, Enum>
  alt_at<enum_pos_c<V>>& emplace(Args&&... args) {
    constexpr std::size_t I = enum_pos_v<V>;
    if constexpr (I == npos_v) {
      static_assert(I != npos_v,
                    "asf::enumvar::var: emplace<V>: V is not an enumerator of Enum");
    } else {
      return emplace_pos<I>(std::forward<Args>(args)...);
    }
  }
  // emplace by alternative type (must occur exactly once)
  template <class T, class... Args>
    requires(detail::type_pos<T, alt_tuple>::count == 1 &&
             std::is_constructible_v<T, Args && ...>)
  T& emplace(Args&&... args) noexcept(
      std::is_nothrow_constructible_v<T, Args && ...>) {
    return emplace_pos<pos_of<T>>(std::forward<Args>(args)...);
  }

  void swap(var& o) noexcept(
      detail::tuple_all<alt_tuple, detail::p_nothrow_move>::value &&
      detail::tuple_all<alt_tuple, detail::p_nothrow_swap>::value)
    requires(detail::tuple_all<alt_tuple, detail::p_swap>::value &&
             detail::tuple_all<alt_tuple, detail::p_move>::value)
  {
    if (this == &o)
      return;
    const Enum ti = storage_.raw_tag(), to = o.storage_.raw_tag();
    if (ti == to) {
      if (!storage_type::is_valueless_tag(ti))
        storage_type::s_dispatch(detail::pos_of_enum<Enum>(ti),
                                 [this, &o](auto ic) {
                                   using std::swap;
                                   swap(*storage_.template ptr<ic.value>(),
                                        *o.storage_.template ptr<ic.value>());
                                 });
      return;
    }
    // stage o's payload, move this's into o, then staged into this
    alignas(storage_type::alignment) unsigned char
        staging[storage_type::payload_bytes];
    if (!storage_type::is_valueless_tag(to))
      storage_type::s_dispatch(
          detail::pos_of_enum<Enum>(to), [&o, &staging](auto ic) {
            using T = std::tuple_element_t<ic.value, alt_tuple>;
            ::new (static_cast<void*>(staging))
                T(std::move(*o.storage_.template ptr<ic.value>()));
          });
    o.storage_.destroy(detail::pos_of_enum<Enum>(to));
    o.storage_.set_valueless();
    if (!storage_type::is_valueless_tag(ti))
      storage_type::s_dispatch(
          detail::pos_of_enum<Enum>(ti), [this, &o](auto ic) {
            o.storage_.template construct<ic.value>(
                std::move(*storage_.template ptr<ic.value>()));
          });
    storage_.destroy(detail::pos_of_enum<Enum>(ti));
    storage_.set_valueless();
    if (!storage_type::is_valueless_tag(to))
      storage_type::s_dispatch(
          detail::pos_of_enum<Enum>(to), [this, &staging](auto ic) {
            using T = std::tuple_element_t<ic.value, alt_tuple>;
            storage_.template construct<ic.value>(
                std::move(*std::launder(reinterpret_cast<T*>(staging))));
          });
  }

  // ---------------- comparisons ----------------
  friend bool operator==(const var& a, const var& b)
    requires(detail::tuple_all<alt_tuple, detail::p_eq>::value)
  {
    const Enum ta = a.storage_.raw_tag(), tb = b.storage_.raw_tag();
    if (ta != tb)
      return false;
    if (storage_type::is_valueless_tag(ta))
      return true;  // both valueless
    return storage_type::s_dispatch(detail::pos_of_enum<Enum>(ta),
                                    [&a, &b](auto ic) {
                                      return *a.storage_.template ptr<
                                                 ic.value>() ==
                                             *b.storage_.template ptr<
                                                 ic.value>();
                                    });
  }

  friend auto operator<=>(const var& a, const var& b)
    requires(detail::tuple_all<alt_tuple, detail::p_3way>::value)
  {
    const Enum ta = a.storage_.raw_tag(), tb = b.storage_.raw_tag();
    if (storage_type::is_valueless_tag(ta) &&
        storage_type::is_valueless_tag(tb))
      return std::strong_ordering::equal;
    if (storage_type::is_valueless_tag(ta))
      return std::strong_ordering::less;
    if (storage_type::is_valueless_tag(tb))
      return std::strong_ordering::greater;
    if (auto c = ta <=> tb; c != 0)
      return c;
    return storage_type::s_dispatch(detail::pos_of_enum<Enum>(ta),
                                    [&a, &b](auto ic) {
                                      return *a.storage_.template ptr<
                                                 ic.value>() <=>
                                             *b.storage_.template ptr<
                                                 ic.value>();
                                    });
  }
};

namespace detail {

// Positional plumbing behind the enum-facing free interface.
template <class Enum, class Tuple>
struct var_access {
  using V = var<Enum, Tuple>;
  static Enum raw(const V& v) { return v.storage_.raw_tag(); }
  template <std::size_t I>
  static auto& pos_get(V& v) {
    return v.template pos_ref<I>();
  }
  template <std::size_t I>
  static auto& pos_get(const V& v) {
    return v.template pos_ref<I>();
  }
  template <std::size_t I>
  static auto&& pos_get(V&& v) {
    return std::move(v).template pos_ref<I>();
  }
  template <std::size_t I>
  static auto&& pos_get(const V&& v) {
    return std::move(v).template pos_ref<I>();
  }
};

// Decompose a var type into its template arguments.
template <class>
struct var_params;
template <class E, class T>
struct var_params<var<E, T>> {
  using enum_t = E;
  using tuple_t = T;
};

// Alternative type for enumerator V of var<Enum, Tuple> (clamped position;
// validity is static_asserted at the use site).
template <auto V, class Enum, class Tuple>
struct enum_alt {
  static constexpr std::size_t pos = index_of_enum<Enum>(V);
  static constexpr std::size_t clamped =
      pos < std::tuple_size_v<typename var<Enum, Tuple>::alt_tuple> ? pos : 0;
  using type =
      std::tuple_element_t<clamped, typename var<Enum, Tuple>::alt_tuple>;
};
template <auto V, class Enum, class Tuple>
using enum_alt_t = typename enum_alt<V, Enum, Tuple>::type;

}  // namespace detail

// ---------------------------------------------------------------------------
// Free interface. Alternatives are identified by enumerator value
// (get<E::x>, get_if<E::x>, holds_alternative<E::x>, on<E::x>) or by
// type (get<T>, get_if<T>, holds_alternative<T>).
// ---------------------------------------------------------------------------

// get by enumerator value (checked; throws std::bad_variant_access)
template <auto V, class Enum, class Tuple>
  requires std::is_same_v<std::remove_cv_t<decltype(V)>, Enum>
detail::enum_alt_t<V, Enum, Tuple>& get(var<Enum, Tuple>& v) {
  constexpr std::size_t I = detail::index_of_enum<Enum>(V);
  static_assert(I != std::variant_npos,
                "asf::enumvar::get<V>: V is not an enumerator of Enum");
  if (detail::var_access<Enum, Tuple>::raw(v) != V)
    throw std::bad_variant_access{};
  return detail::var_access<Enum, Tuple>::template pos_get<I>(v);
}
template <auto V, class Enum, class Tuple>
  requires std::is_same_v<std::remove_cv_t<decltype(V)>, Enum>
const detail::enum_alt_t<V, Enum, Tuple>& get(const var<Enum, Tuple>& v) {
  constexpr std::size_t I = detail::index_of_enum<Enum>(V);
  static_assert(I != std::variant_npos,
                "asf::enumvar::get<V>: V is not an enumerator of Enum");
  if (detail::var_access<Enum, Tuple>::raw(v) != V)
    throw std::bad_variant_access{};
  return detail::var_access<Enum, Tuple>::template pos_get<I>(v);
}
template <auto V, class Enum, class Tuple>
  requires std::is_same_v<std::remove_cv_t<decltype(V)>, Enum>
detail::enum_alt_t<V, Enum, Tuple>&& get(var<Enum, Tuple>&& v) {
  constexpr std::size_t I = detail::index_of_enum<Enum>(V);
  static_assert(I != std::variant_npos,
                "asf::enumvar::get<V>: V is not an enumerator of Enum");
  if (detail::var_access<Enum, Tuple>::raw(v) != V)
    throw std::bad_variant_access{};
  return detail::var_access<Enum, Tuple>::template pos_get<I>(std::move(v));
}
template <auto V, class Enum, class Tuple>
  requires std::is_same_v<std::remove_cv_t<decltype(V)>, Enum>
const detail::enum_alt_t<V, Enum, Tuple>&& get(const var<Enum, Tuple>&& v) {
  constexpr std::size_t I = detail::index_of_enum<Enum>(V);
  static_assert(I != std::variant_npos,
                "asf::enumvar::get<V>: V is not an enumerator of Enum");
  if (detail::var_access<Enum, Tuple>::raw(v) != V)
    throw std::bad_variant_access{};
  return detail::var_access<Enum, Tuple>::template pos_get<I>(std::move(v));
}

// get by alternative type (exactly one occurrence)
template <class T, class Enum, class Tuple>
T& get(var<Enum, Tuple>& v) {
  constexpr std::size_t I =
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::value;
  static_assert(
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::count == 1,
      "asf::enumvar::get<T>: T must occur exactly once");
  if (detail::var_access<Enum, Tuple>::raw(v) != detail::tag_table<Enum>[I])
    throw std::bad_variant_access{};
  return detail::var_access<Enum, Tuple>::template pos_get<I>(v);
}
template <class T, class Enum, class Tuple>
const T& get(const var<Enum, Tuple>& v) {
  constexpr std::size_t I =
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::value;
  static_assert(
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::count == 1,
      "asf::enumvar::get<T>: T must occur exactly once");
  if (detail::var_access<Enum, Tuple>::raw(v) != detail::tag_table<Enum>[I])
    throw std::bad_variant_access{};
  return detail::var_access<Enum, Tuple>::template pos_get<I>(v);
}
template <class T, class Enum, class Tuple>
T&& get(var<Enum, Tuple>&& v) {
  constexpr std::size_t I =
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::value;
  static_assert(
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::count == 1,
      "asf::enumvar::get<T>: T must occur exactly once");
  if (detail::var_access<Enum, Tuple>::raw(v) != detail::tag_table<Enum>[I])
    throw std::bad_variant_access{};
  return detail::var_access<Enum, Tuple>::template pos_get<I>(std::move(v));
}
template <class T, class Enum, class Tuple>
const T&& get(const var<Enum, Tuple>&& v) {
  constexpr std::size_t I =
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::value;
  static_assert(
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::count == 1,
      "asf::enumvar::get<T>: T must occur exactly once");
  if (detail::var_access<Enum, Tuple>::raw(v) != detail::tag_table<Enum>[I])
    throw std::bad_variant_access{};
  return detail::var_access<Enum, Tuple>::template pos_get<I>(std::move(v));
}

// get_if by enumerator value
template <auto V, class Enum, class Tuple>
  requires std::is_same_v<std::remove_cv_t<decltype(V)>, Enum>
detail::enum_alt_t<V, Enum, Tuple>* get_if(var<Enum, Tuple>* v) noexcept {
  constexpr std::size_t I = detail::index_of_enum<Enum>(V);
  static_assert(I != std::variant_npos,
                "asf::enumvar::get_if<V>: V is not an enumerator of Enum");
  if (!v || detail::var_access<Enum, Tuple>::raw(*v) != V)
    return nullptr;
  return &detail::var_access<Enum, Tuple>::template pos_get<I>(*v);
}
template <auto V, class Enum, class Tuple>
  requires std::is_same_v<std::remove_cv_t<decltype(V)>, Enum>
const detail::enum_alt_t<V, Enum, Tuple>* get_if(
    const var<Enum, Tuple>* v) noexcept {
  constexpr std::size_t I = detail::index_of_enum<Enum>(V);
  static_assert(I != std::variant_npos,
                "asf::enumvar::get_if<V>: V is not an enumerator of Enum");
  if (!v || detail::var_access<Enum, Tuple>::raw(*v) != V)
    return nullptr;
  return &detail::var_access<Enum, Tuple>::template pos_get<I>(*v);
}

// get_if by alternative type
template <class T, class Enum, class Tuple>
T* get_if(var<Enum, Tuple>* v) noexcept {
  constexpr std::size_t I =
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::value;
  static_assert(
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::count == 1,
      "asf::enumvar::get_if<T>: T must occur exactly once");
  if (!v || detail::var_access<Enum, Tuple>::raw(*v) !=
                detail::tag_table<Enum>[I])
    return nullptr;
  return &detail::var_access<Enum, Tuple>::template pos_get<I>(*v);
}
template <class T, class Enum, class Tuple>
const T* get_if(const var<Enum, Tuple>* v) noexcept {
  constexpr std::size_t I =
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::value;
  static_assert(
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::count == 1,
      "asf::enumvar::get_if<T>: T must occur exactly once");
  if (!v || detail::var_access<Enum, Tuple>::raw(*v) !=
                detail::tag_table<Enum>[I])
    return nullptr;
  return &detail::var_access<Enum, Tuple>::template pos_get<I>(*v);
}

template <class T, class Enum, class Tuple>
bool holds_alternative(const var<Enum, Tuple>& v) noexcept {
  constexpr std::size_t I =
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::value;
  static_assert(
      detail::type_pos<T, typename var<Enum, Tuple>::alt_tuple>::count == 1,
      "asf::enumvar::holds_alternative<T>: T must occur exactly once");
  return detail::var_access<Enum, Tuple>::raw(v) ==
         detail::tag_table<Enum>[I];
}

// holds_alternative by enumerator value (unlike the type form, this works
// even when several alternatives share one type)
template <auto V, class Enum, class Tuple>
  requires std::is_same_v<std::remove_cv_t<decltype(V)>, Enum>
bool holds_alternative(const var<Enum, Tuple>& v) noexcept {
  static_assert(
      detail::index_of_enum<Enum>(V) != std::variant_npos,
      "asf::enumvar::holds_alternative<V>: V is not an enumerator of Enum");
  return detail::var_access<Enum, Tuple>::raw(v) == V;
}

template <class Enum, class Tuple>
void swap(var<Enum, Tuple>& a, var<Enum, Tuple>& b) noexcept(noexcept(a.swap(b)))
  requires requires { a.swap(b); }
{
  a.swap(b);
}

// visit: checked (throws std::bad_variant_access on valueless).
template <class F, class V>
  requires(detail::is_var_v<std::remove_cvref_t<V>>)
decltype(auto) visit(F&& f, V&& v) {
  if (v.valueless_by_exception())
    throw std::bad_variant_access{};
  using P = detail::var_params<std::remove_cvref_t<V>>;
  using Acc = detail::var_access<typename P::enum_t, typename P::tuple_t>;
  return v.raw_dispatch([&](auto ic) -> decltype(auto) {
    return std::forward<F>(f)(
        Acc::template pos_get<ic.value>(std::forward<V>(v)));
  });
}

template <class F, class V1, class V2, class... Vs>
  requires(detail::is_var_v<std::remove_cvref_t<V1>> &&
           detail::is_var_v<std::remove_cvref_t<V2>> &&
           (detail::is_var_v<std::remove_cvref_t<Vs>> && ...))
decltype(auto) visit(F&& f, V1&& v1, V2&& v2, Vs&&... vs) {
  if (v1.valueless_by_exception() || v2.valueless_by_exception() ||
      (vs.valueless_by_exception() || ...))
    throw std::bad_variant_access{};
  using P = detail::var_params<std::remove_cvref_t<V1>>;
  using Acc = detail::var_access<typename P::enum_t, typename P::tuple_t>;
  return v1.raw_dispatch([&](auto ic) -> decltype(auto) {
    return asf::enumvar::visit(
        [&](auto&&... rest) -> decltype(auto) {
          return std::forward<F>(f)(
              Acc::template pos_get<ic.value>(std::forward<V1>(v1)),
              std::forward<decltype(rest)>(rest)...);
        },
        std::forward<V2>(v2), std::forward<Vs>(vs)...);
  });
}

// ---------------------------------------------------------------------------
// match: enum-keyed visitation (Rust match analogue).
//
//   asf_enumvar::match(v,
//     asf_enumvar::on<Color::red>  ([](Rgb& c)   { ... }),
//     asf_enumvar::on<Color::green, Color::blue>([](auto&) { ... }),
//     [](auto&) { ... });   // optional FINAL fallback for the unarmed rest
//
// An on<Vs...> arm covers one or MORE enumerators with a single callable.
// The LAST argument is the fallback iff it is not an on<> arm (a type
// check); with no fallback the arms must cover every enumerator.
// Compile-time checked: every armed enumerator must belong to the matched
// enum and no enumerator may be armed twice. Arms and the fallback are
// invoked with the active alternative (an arm for a std::monostate
// alternative, or the fallback, may be nullary instead); all must yield
// the same return type. Runtime-checked like visit (throws
// std::bad_variant_access on valueless).
//
// A single plain callable (e.g. one generic lambda) is the all-fallback
// case, dispatching like visit:
//
//   asf_enumvar::match(v, [](auto& alt) { ... });
// ---------------------------------------------------------------------------
namespace detail {

template <class F, auto... Vs>
struct match_arm {
  F fn;
};

template <class>
struct is_match_arm : std::false_type {};
template <class F, auto... Vs>
struct is_match_arm<match_arm<F, Vs...>> : std::true_type {};

// Every argument but possibly the last must be an on<> arm; a non-arm
// last argument is the fallback.
template <class... Args>
inline constexpr bool valid_match_args_v =
    []<std::size_t... I>(std::index_sequence<I...>) {
      return ((I + 1 == sizeof...(Args) ||
               is_match_arm<
                   std::tuple_element_t<I, std::tuple<Args...>>>::value) &&
              ...);
    }(std::make_index_sequence<sizeof...(Args)>{});

template <std::size_t N>
struct match_coverage_t {
  std::array<std::size_t, N> for_pos{};  // arm index per alternative position
  bool valid = true;    // every armed enumerator names the matched enum
  bool nodup = true;    // no enumerator armed twice
  bool covered = true;  // every position armed
};

template <class Enum, auto V, class Cov>
consteval void match_cover_one(Cov& r, std::size_t k) {
  if constexpr (std::is_same_v<std::remove_cv_t<decltype(V)>, Enum>) {
    constexpr std::size_t pos = index_of_enum<Enum>(V);
    if constexpr (pos == std::variant_npos) {
      r.valid = false;
    } else if (r.for_pos[pos] != std::variant_npos) {
      r.nodup = false;
    } else {
      r.for_pos[pos] = k;
    }
  } else {
    r.valid = false;
  }
}

template <class Enum, class Cov, class F, auto... Vs>
consteval void match_cover_arm(Cov& r, std::size_t k,
                               match_arm<F, Vs...> const*) {
  (match_cover_one<Enum, Vs>(r, k), ...);
}

template <class Enum, std::size_t N, class... Arms>
consteval auto match_coverage() {
  match_coverage_t<N> r;
  r.for_pos.fill(std::variant_npos);
  std::size_t k = 0;
  ((match_cover_arm<Enum>(r, k, static_cast<Arms const*>(nullptr)), ++k),
   ...);
  for (std::size_t i = 0; i < N; ++i)
    if (r.for_pos[i] == std::variant_npos)
      r.covered = false;
  return r;
}

template <class V, class Tup, std::size_t... ArmI, bool HasFb>
decltype(auto) match_impl(V&& v, Tup& tup, std::index_sequence<ArmI...>,
                          std::bool_constant<HasFb>) {
  using V0 = std::remove_cvref_t<V>;
  using P = var_params<V0>;
  using Enum = typename P::enum_t;
  using Acc = var_access<Enum, typename P::tuple_t>;
  constexpr auto cov = match_coverage<Enum, V0::size(),
                                      std::tuple_element_t<ArmI, Tup>...>();
  static_assert(cov.valid,
                "asf::enumvar::match: every on<V> must name an enumerator of "
                "the matched enum");
  static_assert(cov.nodup,
                "asf::enumvar::match: duplicate on<V> for one enumerator");
  static_assert(HasFb || cov.covered,
                "asf::enumvar::match: non-exhaustive; every enumerator needs "
                "an on arm or a final fallback");
  if constexpr (!(cov.valid && cov.nodup && (HasFb || cov.covered))) {
    return;  // unreachable: the static_asserts above already fired
  } else {
    if (v.valueless_by_exception())
      throw std::bad_variant_access{};
    return v.raw_dispatch([&](auto ic) -> decltype(auto) {
      auto&& alt = Acc::template pos_get<ic.value>(std::forward<V>(v));
      if constexpr (cov.for_pos[ic.value] != std::variant_npos) {
        auto& arm_fn = std::get<cov.for_pos[ic.value]>(tup).fn;
        static_assert(std::is_invocable_v<decltype(arm_fn), decltype(alt)> ||
                          std::is_invocable_v<decltype(arm_fn)>,
                      "asf::enumvar::match: on arm must be callable with the "
                      "alternative (or nullary for std::monostate)");
        if constexpr (std::is_invocable_v<decltype(arm_fn), decltype(alt)>)
          return std::invoke(arm_fn, std::forward<decltype(alt)>(alt));
        else
          return std::invoke(arm_fn);
      } else {
        auto& fb = std::get<sizeof...(ArmI)>(tup);
        static_assert(std::is_invocable_v<decltype(fb), decltype(alt)> ||
                          std::is_invocable_v<decltype(fb)>,
                      "asf::enumvar::match: fallback must be callable with "
                      "the alternative (or nullary)");
        if constexpr (std::is_invocable_v<decltype(fb), decltype(alt)>)
          return std::invoke(fb, std::forward<decltype(alt)>(alt));
        else
          return std::invoke(fb);
      }
    });
  }
}

}  // namespace detail

// on<Vs...>(f): one arm of asf::match, keyed by one or MORE enumerators.
template <auto... Vs, class F>
  requires(sizeof...(Vs) >= 1)
constexpr detail::match_arm<std::decay_t<F>, Vs...> on(F&& f) {
  return {std::forward<F>(f)};
}

template <class V, class... Args>
  requires(detail::is_var_v<std::remove_cvref_t<V>> && (sizeof...(Args) > 0) &&
           detail::valid_match_args_v<std::remove_cvref_t<Args>...>)
decltype(auto) match(V&& v, Args&&... args) {
  using Last = std::tuple_element_t<sizeof...(Args) - 1,
                                    std::tuple<std::remove_cvref_t<Args>...>>;
  constexpr bool has_fb = !detail::is_match_arm<Last>::value;
  auto tup =
      std::tuple<std::remove_cvref_t<Args>...>(std::forward<Args>(args)...);
  return detail::match_impl(
      std::forward<V>(v), tup,
      std::make_index_sequence<sizeof...(Args) - (has_fb ? 1 : 0)>{},
      std::bool_constant<has_fb>{});
}

}  // namespace enumvar
}  // namespace asf

namespace asf_enumvar = asf::enumvar;

// ---------------------------------------------------------------------------
// std integration: variant_size / variant_alternative
// ---------------------------------------------------------------------------
namespace std {

template <class Enum, class Tuple>
struct variant_size<asf::enumvar::var<Enum, Tuple>>
    : integral_constant<size_t, tuple_size_v<typename asf::enumvar::var<Enum, Tuple>::alt_tuple>> {};

template <size_t I, class Enum, class Tuple>
struct variant_alternative<I, asf::enumvar::var<Enum, Tuple>> {
  using type = tuple_element_t<I, typename asf::enumvar::var<Enum, Tuple>::alt_tuple>;
};

}  // namespace std
