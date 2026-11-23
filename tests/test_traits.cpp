// Trait parity matrix: asf_enumvar::var<E, tuple<A, B>> vs std::variant<A, B> for a
// battery of alternative shapes. Every special-member trait must agree,
// including the deleted states ([variant.dtor/ctor/assign]).
#include <enumvar.hpp>

#include <string>
#include <type_traits>
#include <variant>

namespace {

enum class E2 { a, b };

struct Triv {
  int x;
};
struct NDtor {  // non-trivial dtor; copy/move ctors still trivial
  int x;
  ~NDtor() {}
};
struct NCopy {  // user copy ctor (non-trivial); move falls back to copy
  int x;
  NCopy() = default;
  NCopy(const NCopy&) {}
};
struct NMove {  // user move ctor; implicit copy ctor/assign deleted
  int x;
  NMove() = default;
  NMove(NMove&&) {}
};
struct MoveOnly {
  int x;
  MoveOnly() = default;
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&&) = default;
};
struct CAssign {  // user copy assignment (non-trivial)
  int x;
  CAssign& operator=(const CAssign&) { return *this; }
};
struct NoCopyAssign {
  int x;
  NoCopyAssign() = default;
  NoCopyAssign(NoCopyAssign&&) = default;
  NoCopyAssign& operator=(const NoCopyAssign&) = delete;
};
struct NoMoveAssign {
  int x;
  NoMoveAssign() = default;
  NoMoveAssign(const NoMoveAssign&) = default;
  NoMoveAssign& operator=(NoMoveAssign&&) = delete;
};
struct NoDef {
  int x;
  NoDef() = delete;
};

template <class... Ts>
consteval bool traits_match() {
  using V = asf_enumvar::var<E2, std::tuple<Ts...>>;
  using S = std::variant<Ts...>;
  bool ok = true;
  ok &= std::is_destructible_v<V> == std::is_destructible_v<S>;
  ok &= std::is_trivially_destructible_v<V> ==
        std::is_trivially_destructible_v<S>;
  ok &= std::is_copy_constructible_v<V> == std::is_copy_constructible_v<S>;
  ok &= std::is_trivially_copy_constructible_v<V> ==
        std::is_trivially_copy_constructible_v<S>;
  ok &= std::is_move_constructible_v<V> == std::is_move_constructible_v<S>;
  ok &= std::is_trivially_move_constructible_v<V> ==
        std::is_trivially_move_constructible_v<S>;
  ok &= std::is_copy_assignable_v<V> == std::is_copy_assignable_v<S>;
  ok &= std::is_trivially_copy_assignable_v<V> ==
        std::is_trivially_copy_assignable_v<S>;
  ok &= std::is_move_assignable_v<V> == std::is_move_assignable_v<S>;
  ok &= std::is_trivially_move_assignable_v<V> ==
        std::is_trivially_move_assignable_v<S>;
  ok &= std::is_default_constructible_v<V> == std::is_default_constructible_v<S>;
  return ok;
}

#define MATCH(...) static_assert(traits_match<__VA_ARGS__>(), #__VA_ARGS__);

MATCH(Triv, double)
MATCH(Triv, NDtor)
MATCH(NDtor, NDtor)
MATCH(Triv, NCopy)
MATCH(NCopy, NCopy)
MATCH(Triv, NMove)
MATCH(NMove, NMove)
MATCH(Triv, MoveOnly)
MATCH(MoveOnly, MoveOnly)
MATCH(Triv, CAssign)
MATCH(CAssign, CAssign)
MATCH(Triv, NoCopyAssign)
MATCH(NoCopyAssign, NoCopyAssign)
MATCH(Triv, NoMoveAssign)
MATCH(NoMoveAssign, NoMoveAssign)
MATCH(std::string, Triv)
MATCH(std::string, std::string)
MATCH(NMove, NoCopyAssign)
MATCH(MoveOnly, NoMoveAssign)
MATCH(NDtor, CAssign)
MATCH(NoDef, Triv)

#undef MATCH

}  // namespace

int main() {}
