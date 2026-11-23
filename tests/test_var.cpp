// asf_enumvar test: annotation mode / tuple mode / empty tuple mode,
// enum-keyed interface (index() returns the enumerator, get<E::x>,
// match/on), triviality, embedded tag.
#include <enumvar.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <type_traits>

namespace {

// ---------------- fixtures ----------------
struct Circle {
  double r;
  double area() const { return 3.141592653589793 * r * r; }
  double scale_by(double k) {
    r *= k;
    return r;
  }
};
struct Rect {
  double w, h;
  double area() const { return w * h; }
};
struct Blob {
  std::string s;
  int size_of() const { return static_cast<int>(s.size()); }
  bool operator==(const Blob&) const = default;
  auto operator<=>(const Blob&) const = default;
};

// annotation mode
enum class Shape {
  circle [[=asf_enumvar::alt<Circle>]],
  rect [[=asf_enumvar::alt<Rect>]],
  blob [[=asf_enumvar::alt<Blob>]],
  empty  // no annotation -> std::monostate
};

// fieldless enum
enum class Color { red, green, blue };

// explicit values (Rust-style explicit discriminants)
enum class Code : int { ok = 0, not_found = 404, server_err = 500 };

using ShapeVar = asf_enumvar::var<Shape>;                 // void: annotation mode
using ColorVar = asf_enumvar::var<Color>;                 // void, no annotations -> all monostate
using ColorVar2 = asf_enumvar::var<Color, std::tuple<>>;  // explicit: tuple too short -> all fieldless
using CodeVar = asf_enumvar::var<Code, std::tuple<int, std::string, Blob>>;

// tuple shorter than the enum: trailing enumerators are pure values
enum class Flavor { vanilla, choco, mint };
using FlavorVar = asf_enumvar::var<Flavor, std::tuple<std::string>>;

// std::monostate placeholder: this position is deliberately fieldless
enum class Task { idle, run, stop };
using TaskVar =
    asf_enumvar::var<Task, std::tuple<std::monostate, std::string, std::monostate>>;

// std::monostate in annotation mode: explicit "fieldless" marker
enum class Sig {
  ping [[=asf_enumvar::alt<std::monostate>]],
  pong [[=asf_enumvar::alt<int>]]
};
using SigVar = asf_enumvar::var<Sig>;

// tiny fieldless enum: the var is exactly the enum (Rust layout)
enum class Small : unsigned char { x, y };
using SmallVar = asf_enumvar::var<Small>;

// single-enumerator enum: converting assignment from monostate reaches the
// same-tag assign path
enum class Solo { only };
using SoloVar = asf_enumvar::var<Solo>;
static_assert(sizeof(SoloVar) == sizeof(Solo));

// aliased enumerators (same value) with the same alternative type: legal
enum class Dup : int { a = 1, b = 1, c = 2 };
using DupVar = asf_enumvar::var<Dup, std::tuple<int, int, std::string>>;

// converting ctor/assignment: automatic matching requires a UNIQUE match;
// two identical payload types make it ill-formed (tag<> disambiguates)
static_assert(std::is_constructible_v<ShapeVar, Circle>);
static_assert(std::is_constructible_v<DupVar, std::string>);
static_assert(!std::is_constructible_v<DupVar, int>);
static_assert(!std::is_assignable_v<DupVar&, int>);

// extreme enumerator values: the valueless sentinel must dodge both ends
enum class Edge : int { lo = -2147483647 - 1, hi = 2147483647 };
using EdgeVar = asf_enumvar::var<Edge, std::tuple<int, double>>;

// ---------------- static checks ----------------
// alternatives resolved from annotations
static_assert(std::is_same_v<std::variant_alternative_t<0, ShapeVar>, Circle>);
static_assert(std::is_same_v<std::variant_alternative_t<1, ShapeVar>, Rect>);
static_assert(std::is_same_v<std::variant_alternative_t<2, ShapeVar>, Blob>);
static_assert(std::is_same_v<std::variant_alternative_t<3, ShapeVar>,
                             std::monostate>);
static_assert(std::variant_size_v<ShapeVar> == 4);
static_assert(std::variant_size_v<ColorVar> == 3);

// short tuple: positional payload, rest fieldless
static_assert(std::variant_size_v<FlavorVar> == 3);
static_assert(
    std::is_same_v<std::variant_alternative_t<0, FlavorVar>, std::string>);
static_assert(
    std::is_same_v<std::variant_alternative_t<1, FlavorVar>, std::monostate>);
static_assert(
    std::is_same_v<std::variant_alternative_t<2, FlavorVar>, std::monostate>);

// std::monostate placeholder positions (both tuple and annotation modes)
static_assert(
    std::is_same_v<std::variant_alternative_t<0, TaskVar>, std::monostate>);
static_assert(
    std::is_same_v<std::variant_alternative_t<1, TaskVar>, std::string>);
static_assert(
    std::is_same_v<std::variant_alternative_t<2, TaskVar>, std::monostate>);
static_assert(
    std::is_same_v<std::variant_alternative_t<0, SigVar>, std::monostate>);
static_assert(std::is_same_v<std::variant_alternative_t<1, SigVar>, int>);

// enum tag table / index_of
static_assert(ShapeVar::index_of(Shape::rect) == 1);
static_assert(CodeVar::index_of(Code::not_found) == 1);
static_assert(ColorVar::index_of(Color::blue) == 2);

// index() returns the enumerator
static_assert(std::is_same_v<decltype(std::declval<ShapeVar&>().index()),
                             Shape>);
static_assert(std::is_same_v<decltype(std::declval<const ColorVar&>().index()),
                             Color>);

// triviality propagation
static_assert(std::is_trivially_destructible_v<ColorVar>);
static_assert(std::is_trivially_copy_constructible_v<ColorVar>);
static_assert(std::is_trivially_move_constructible_v<ColorVar>);
static_assert(std::is_trivially_copy_assignable_v<ColorVar>);
static_assert(std::is_trivially_move_assignable_v<ColorVar>);
static_assert(std::is_trivially_copyable_v<ColorVar>);
static_assert(!std::is_trivially_destructible_v<ShapeVar>);   // Blob/string
static_assert(!std::is_trivially_destructible_v<CodeVar>);

// fieldless: the discriminant is a real Enum member and the monostate
// payload overlaps it ([[no_unique_address]]) -- the var IS the enum.
static_assert(sizeof(ColorVar) == sizeof(Color));
static_assert(sizeof(SmallVar) == sizeof(Small));  // 1 byte
static_assert(sizeof(ShapeVar) <=
              sizeof(std::string) + 2 * sizeof(std::size_t));  // Blob + tag

// ---------------- runtime tests ----------------
void test_annotation_mode() {
  ShapeVar v;  // default: alternative 0
  assert(v.index() == Shape::circle);
  assert(asf_enumvar::holds_alternative<Circle>(v));
  assert(asf_enumvar::holds_alternative<Shape::circle>(v));  // by enumerator
  assert(!asf_enumvar::holds_alternative<Shape::rect>(v));

  asf_enumvar::get<Circle>(v).r = 2.0;
  assert(std::fabs(asf_enumvar::get<Shape::circle>(v).area() -
                   4 * 3.141592653589793) < 1e-9);

  v.emplace<Rect>(3.0, 4.0);
  assert(v.index() == Shape::rect);
  assert(asf_enumvar::get<Rect>(v).area() == 12.0);

  v.emplace<Blob>("hello");
  assert(asf_enumvar::get<Blob>(v).size_of() == 5);

  v.emplace<Shape::empty>();  // monostate
  assert(v.index() == Shape::empty);
  assert(asf_enumvar::holds_alternative<std::monostate>(v));

  // in_place / tag construction
  ShapeVar w(std::in_place_type<Rect>, 2.0, 5.0);
  assert(asf_enumvar::get<Rect>(w).area() == 10.0);
  ShapeVar t(asf_enumvar::tag<Shape::blob>, "payload");
  assert(t.index() == Shape::blob);
  assert(asf_enumvar::get<Blob>(t).s == "payload");

  // converting ctor: Circle is the only alternative constructible from Circle
  ShapeVar c = Circle{1.5};
  assert(asf_enumvar::holds_alternative<Circle>(c));

  // get_if: by type and by enumerator
  assert(asf_enumvar::get_if<Circle>(&c) != nullptr);
  assert(asf_enumvar::get_if<Rect>(&c) == nullptr);
  assert(asf_enumvar::get_if<Shape::rect>(&c) == nullptr);
  assert(asf_enumvar::get_if<Shape::circle>(&c) != nullptr);

  // bad access throws
  bool threw = false;
  try {
    (void)asf_enumvar::get<Rect>(c);
  } catch (const std::bad_variant_access&) {
    threw = true;
  }
  assert(threw);

  // get<V> throws for the wrong enumerator too
  threw = false;
  try {
    (void)asf_enumvar::get<Shape::blob>(c);
  } catch (const std::bad_variant_access&) {
    threw = true;
  }
  assert(threw);
}

void test_fieldless() {
  ColorVar v;  // red
  assert(v.index() == Color::red);
  v.emplace<Color::blue>();
  assert(v.index() == Color::blue);

  ColorVar2 v2;
  static_assert(std::is_same_v<decltype(v), decltype(v2)> == false);
  assert(v2.index() == Color::red);

  // copy/trivial roundtrip
  ColorVar a, b;
  a.emplace<Color::green>();
  b = a;
  assert(b.index() == Color::green);

  // 1-byte fieldless var through the generic interface
  SmallVar s;
  assert(s.index() == Small::x);
  s.emplace<Small::y>();
  assert(s.index() == Small::y);
  SmallVar s2 = s;
  assert(s2.index() == Small::y);

  // same-tag monostate assignment must leave the tag intact
  SoloVar solo;
  solo = std::monostate{};
  assert(solo.index() == Solo::only);
}

void test_tuple_mode() {
  CodeVar v(asf_enumvar::tag<Code::not_found>, std::string("missing"));
  assert(v.index() == Code::not_found);
  assert(asf_enumvar::get<std::string>(v) == "missing");

  v = 42;  // converting assignment -> int alternative
  assert(v.index() == Code::ok);
  assert(asf_enumvar::get<int>(v) == 42);

  // short tuple: vanilla carries std::string, choco/mint are pure enum values
  FlavorVar f(asf_enumvar::tag<Flavor::vanilla>, std::string("bean"));
  assert(asf_enumvar::get<std::string>(f) == "bean");
  f.emplace<Flavor::mint>();
  assert(f.index() == Flavor::mint);
  // dup monostate types: type-based access is ambiguous, enum-based is not
  (void)asf_enumvar::get<Flavor::mint>(f);

  // std::monostate placeholder positions behave like any fieldless slot
  TaskVar t;
  assert(t.index() == Task::idle);
  t.emplace<Task::run>(std::string("running"));
  assert(asf_enumvar::get<Task::run>(t) == "running");
  t.emplace<Task::stop>();
  assert(t.index() == Task::stop);

  SigVar s;
  assert(s.index() == Sig::ping);
  s.emplace<Sig::pong>(7);
  assert(asf_enumvar::get<int>(s) == 7);
}

void test_visit() {
  ShapeVar v(asf_enumvar::tag<Shape::circle>, Circle{2.0});
  double a = asf_enumvar::visit(
      [](auto& x) -> double {
        if constexpr (requires { x.area(); })
          return x.area();
        else
          return -1.0;
      },
      v);
  assert(a > 12.5);

  v.emplace<Shape::empty>();
  a = asf_enumvar::visit(
      [](auto& x) -> double {
        if constexpr (requires { x.area(); })
          return x.area();
        else
          return -1.0;
      },
      v);
  assert(a == -1.0);

  // multi-var visit
  ShapeVar x(asf_enumvar::tag<Shape::rect>, Rect{2.0, 3.0});
  ShapeVar y(asf_enumvar::tag<Shape::circle>, Circle{1.0});
  double sum = asf_enumvar::visit(
      [](auto& p, auto& q) -> double {
        auto area = [](auto& z) -> double {
          if constexpr (requires { z.area(); })
            return z.area();
          else
            return 0.0;
        };
        return area(p) + area(q);
      },
      x, y);
  assert(std::fabs(sum - (6.0 + 3.141592653589793)) < 1e-9);
}

void test_match() {
  // arms keyed by enumerator value; monostate arm may be nullary
  ShapeVar v(asf_enumvar::tag<Shape::circle>, Circle{2.0});
  double a = asf_enumvar::match(
      v,
      asf_enumvar::on<Shape::circle>([](Circle& c) { return c.area(); }),
      asf_enumvar::on<Shape::rect>([](Rect& r) { return r.area(); }),
      asf_enumvar::on<Shape::blob>(
          [](Blob& b) { return static_cast<double>(b.size_of()); }),
      asf_enumvar::on<Shape::empty>([] { return -1.0; }));
  assert(std::fabs(a - 4 * 3.141592653589793) < 1e-9);

  // arm order is irrelevant (dispatch is by enumerator, not position)
  v.emplace<Shape::empty>();
  a = asf_enumvar::match(
      v,
      asf_enumvar::on<Shape::empty>([] { return -1.0; }),
      asf_enumvar::on<Shape::blob>(
          [](Blob& b) { return static_cast<double>(b.size_of()); }),
      asf_enumvar::on<Shape::rect>([](Rect& r) { return r.area(); }),
      asf_enumvar::on<Shape::circle>([](Circle& c) { return c.area(); }));
  assert(a == -1.0);

  // mutation through the active arm (void return)
  ShapeVar m(asf_enumvar::tag<Shape::rect>, 2.0, 5.0);
  asf_enumvar::match(
      m,
      asf_enumvar::on<Shape::circle>([](Circle&) {}),
      asf_enumvar::on<Shape::rect>([](Rect& r) { r.w = 10.0; }),
      asf_enumvar::on<Shape::blob>([](Blob&) {}),
      asf_enumvar::on<Shape::empty>([] {}));
  assert(asf_enumvar::get<Shape::rect>(m).w == 10.0);

  // duplicate monostate alternatives: match still selects by enumerator
  FlavorVar f(asf_enumvar::tag<Flavor::mint>);
  int which = asf_enumvar::match(
      f,
      asf_enumvar::on<Flavor::vanilla>(
          [](std::string& s) { return static_cast<int>(s.size()); }),
      asf_enumvar::on<Flavor::choco>([] { return 100; }),
      asf_enumvar::on<Flavor::mint>([] { return 200; }));
  assert(which == 200);

  // const var: arms receive const refs (or none)
  const ColorVar c;
  int k = asf_enumvar::match(
      c,
      asf_enumvar::on<Color::red>([] { return 1; }),
      asf_enumvar::on<Color::green>([] { return 2; }),
      asf_enumvar::on<Color::blue>([] { return 3; }));
  assert(k == 1);

  // rvalue var
  k = asf_enumvar::match(
      ColorVar(asf_enumvar::tag<Color::blue>),
      asf_enumvar::on<Color::red>([] { return 1; }),
      asf_enumvar::on<Color::green>([] { return 2; }),
      asf_enumvar::on<Color::blue>([] { return 3; }));
  assert(k == 3);

  // a monostate arm may also take the alternative explicitly
  ColorVar g(asf_enumvar::tag<Color::green>);
  k = asf_enumvar::match(
      g,
      asf_enumvar::on<Color::red>([](const std::monostate&) { return 1; }),
      asf_enumvar::on<Color::green>([](const std::monostate&) { return 2; }),
      asf_enumvar::on<Color::blue>([](const std::monostate&) { return 3; }));
  assert(k == 2);

  // single generic lambda: visit-style dispatch through match
  ShapeVar one(asf_enumvar::tag<Shape::circle>, Circle{3.0});
  double ga = asf_enumvar::match(one, [](auto& x) -> double {
    if constexpr (requires { x.area(); })
      return x.area();
    else
      return 0.0;
  });
  assert(ga > 28.0);

  // single callable on const var
  const ColorVar cc;  // red
  int n = asf_enumvar::match(cc, [](auto&&) { return 42; });
  assert(n == 42);

  // on<Vs...>: one arm covering several enumerators
  ColorVar g2(asf_enumvar::tag<Color::green>);
  k = asf_enumvar::match(
      g2,
      asf_enumvar::on<Color::red, Color::green>([] { return 12; }),
      asf_enumvar::on<Color::blue>([] { return 3; }));
  assert(k == 12);

  // multi-enumerator arm receiving the alternative (generic)
  k = asf_enumvar::match(
      g2,
      asf_enumvar::on<Color::red>([] { return 1; }),
      asf_enumvar::on<Color::green, Color::blue>(
          [](const std::monostate& m) {
            return m == std::monostate{} ? 23 : -1;
          }));
  assert(k == 23);

  // final fallback: a non-arm last argument (detected by type check) --
  // the arms no longer need to be exhaustive
  ShapeVar cv2(asf_enumvar::tag<Shape::circle>, Circle{2.0});
  double d = asf_enumvar::match(
      cv2,
      asf_enumvar::on<Shape::circle>([](Circle& c) { return c.area(); }),
      [](auto&) { return -1.0; });
  assert(d > 12.0);  // armed branch taken
  v.emplace<Shape::empty>();
  d = asf_enumvar::match(
      v,
      asf_enumvar::on<Shape::circle>([](Circle& c) { return c.area(); }),
      [](auto&) { return -1.0; });
  assert(d == -1.0);  // everything else falls through

  // nullary fallback; multi-enumerator arm + fallback combined
  ShapeVar rv2(asf_enumvar::tag<Shape::rect>, Rect{2.0, 5.0});
  d = asf_enumvar::match(
      rv2,
      asf_enumvar::on<Shape::rect, Shape::blob>([](auto&) { return 0.5; }),
      asf_enumvar::on<Shape::circle>([](Circle& c) { return c.area(); }),
      [] { return -2.0; });
  assert(d == 0.5);
  v.emplace<Shape::empty>();
  d = asf_enumvar::match(
      v,
      asf_enumvar::on<Shape::rect, Shape::blob>([](auto&) { return 0.5; }),
      asf_enumvar::on<Shape::circle>([](Circle& c) { return c.area(); }),
      [] { return -2.0; });
  assert(d == -2.0);

  // the nullary fallback also serves PAYLOAD alternatives it ignores
  // (rv2 holds a Rect; rect/blob/empty all fall through)
  d = asf_enumvar::match(
      rv2,
      asf_enumvar::on<Shape::circle>([](Circle& c) { return c.area(); }),
      [] { return -7.0; });
  assert(d == -7.0);

  // a typed fallback is checked against every UNCOVERED alternative
  int e9 = asf_enumvar::match(
      v,
      asf_enumvar::on<Shape::circle>([](Circle&) { return 1; }),
      asf_enumvar::on<Shape::rect>([](Rect&) { return 2; }),
      asf_enumvar::on<Shape::blob>([](Blob&) { return 3; }),
      [](std::monostate&) { return 9; });  // only empty is uncovered
  assert(e9 == 9);
}

void test_aliased_enumerators() {
  // b aliases a (both value 1); value lookups select the first enumerator,
  // and enum comparisons are by value, so both spellings agree
  DupVar v(asf_enumvar::tag<Dup::b>, 42);
  assert(v.index() == Dup::a);
  assert(v.index() == Dup::b);
  assert(asf_enumvar::get<Dup::a>(v) == 42);  // int occurs twice: enum-keyed access
  assert(asf_enumvar::get<Dup::b>(v) == 42);
  // holds_alternative by enumerator: works despite the duplicate int type
  assert(asf_enumvar::holds_alternative<Dup::a>(v));
  assert(asf_enumvar::holds_alternative<Dup::b>(v));  // same value as a
  assert(!asf_enumvar::holds_alternative<Dup::c>(v));

  v.emplace<Dup::c>(std::string("x"));
  assert(v.index() == Dup::c);
  assert(asf_enumvar::get<Dup::c>(v) == "x");
  assert(asf_enumvar::holds_alternative<Dup::c>(v));

  // extreme values: construction/dispatch still work
  EdgeVar ev(asf_enumvar::tag<Edge::hi>, 1.5);
  assert(ev.index() == Edge::hi);
  assert(asf_enumvar::get<Edge::hi>(ev) == 1.5);
  EdgeVar el(asf_enumvar::tag<Edge::lo>, -7);
  assert(el.index() == Edge::lo);
}

void test_valueless() {
  struct Thrower {
    Thrower() { throw 42; }
  };
  enum class X { a [[=asf_enumvar::alt<int>]], b [[=asf_enumvar::alt<Thrower>]] };
  using XV = asf_enumvar::var<X>;
  XV v;
  assert(!v.valueless_by_exception());
  try {
    v.emplace<X::b>();
    assert(false);
  } catch (int) {
  }
  assert(v.valueless_by_exception());

  // recover via emplace
  v.emplace<X::a>(7);
  assert(asf_enumvar::get<int>(v) == 7);

  // visit and match throw on valueless
  XV e;
  e.emplace<X::a>(1);
  try {
    e.emplace<X::b>();  // Thrower ctor throws -> e becomes valueless
    assert(false);
  } catch (int) {
  }
  bool threw = false;
  try {
    asf_enumvar::visit([](auto&) {}, e);
  } catch (const std::bad_variant_access&) {
    threw = true;
  }
  assert(threw);

  threw = false;
  try {
    asf_enumvar::match(
        e,
        asf_enumvar::on<X::a>([](int&) {}),
        asf_enumvar::on<X::b>([](Thrower&) {}));
  } catch (const std::bad_variant_access&) {
    threw = true;
  }
  assert(threw);
}

void test_swap_and_compare() {
  ColorVar a, b;
  a.emplace<Color::green>();
  b.emplace<Color::blue>();
  swap(a, b);
  assert(a.index() == Color::blue);
  assert(b.index() == Color::green);
  assert(a != b);
  a.swap(b);  // swapped back
  assert(a.index() == Color::green);
  assert(b.index() == Color::blue);

  // same-tag swap: swapping two monostates must not touch the tag
  ColorVar c1, c2;
  c1.emplace<Color::green>();
  c2.emplace<Color::green>();
  c1.swap(c2);
  assert(c1.index() == Color::green);
  assert(c2.index() == Color::green);

  CodeVar x(asf_enumvar::tag<Code::ok>, 1);
  CodeVar y(asf_enumvar::tag<Code::ok>, 2);
  assert(x < y);
  x = y;
  assert(x == y);
}

void test_move_only() {
  struct MO {
    int x;
    explicit MO(int v) : x(v) {}
    MO(const MO&) = delete;
    MO(MO&&) = default;
    MO& operator=(const MO&) = delete;
    MO& operator=(MO&&) = default;
  };
  enum class E { a [[=asf_enumvar::alt<int>]], b [[=asf_enumvar::alt<MO>]] };
  using XV = asf_enumvar::var<E>;
  static_assert(!std::is_copy_constructible_v<XV>);   // deleted, like std
  static_assert(!std::is_copy_assignable_v<XV>);      // deleted, like std
  static_assert(std::is_move_constructible_v<XV>);
  static_assert(std::is_move_assignable_v<XV>);
  XV v(asf_enumvar::tag<E::b>, 5);
  XV w = std::move(v);
  assert(asf_enumvar::get<MO>(w).x == 5);
  XV u(asf_enumvar::tag<E::a>, 1);
  u = std::move(w);
  assert(u.index() == E::b);
}

}  // namespace

int main() {
  test_annotation_mode();
  test_fieldless();
  test_tuple_mode();
  test_visit();
  test_match();
  test_aliased_enumerators();
  test_valueless();
  test_swap_and_compare();
  test_move_only();
  std::puts("all asf_enumvar tests passed");
}
