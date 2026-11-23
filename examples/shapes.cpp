// asf_enumvar example -- annotation mode: payload types ride on the
// enumerators; enum-keyed construction, access, and a match with
// multi-enumerator arms plus a final fallback.
//
// Examples are narrative demos meant for reading; correctness checks
// (assertions, edge cases, static_assert batteries) live in tests/.

#include <enumvar.hpp>

#include <print>
#include <string_view>
#include <utility>

struct Circle {
  double r;
  double area() const { return 3.141592653589793 * r * r; }
};
struct Rect {
  double w, h;
  double area() const { return w * h; }
};
struct Triangle {
  double base, height;
};

// One enumerator, one payload type. `empty` has no annotation, so it is
// fieldless (std::monostate).
enum class Shape {
  circle [[=asf_enumvar::alt<Circle>]],
  rect [[=asf_enumvar::alt<Rect>]],
  triangle [[=asf_enumvar::alt<Triangle>]],
  empty,
};
using ShapeVar = asf_enumvar::var<Shape>;

std::string_view name_of(Shape s) {
  switch (s) {
    case Shape::circle: return "circle";
    case Shape::rect: return "rect";
    case Shape::triangle: return "triangle";
    case Shape::empty: return "empty";
  }
  std::unreachable();
}

double area(const ShapeVar& v) {
  return asf_enumvar::match(
      v,
      // one arm covering two enumerators with a single callable
      asf_enumvar::on<Shape::circle, Shape::rect>(
          [](const auto& s) { return s.area(); }),
      // a typed arm for one enumerator
      asf_enumvar::on<Shape::triangle>(
          [](const Triangle& t) { return 0.5 * t.base * t.height; }),
      // the final fallback: not an on<> arm, so it catches the rest
      [] { return 0.0; });
}

int main() {
  // Automatic matching (std::variant-style converting ctor): the
  // argument type picks the alternative. It is ILL-FORMED when more
  // than one alternative matches -- e.g. two enumerators carrying the
  // same type; tag<> below is the disambiguator for that case.
  ShapeVar shapes[] = {
      ShapeVar(Circle{2.0}),
      ShapeVar(Rect{3.0, 4.0}),
      ShapeVar(Triangle{6.0, 2.0}),
      ShapeVar(std::monostate{}),  // the sole monostate -> empty
  };
  for (const ShapeVar& v : shapes)
    std::println("{:<8} area = {}", name_of(v.index()), area(v));

  // Explicit tag spelling: always works, even when automatic matching
  // is ambiguous (duplicate payload types).
  ShapeVar v(asf_enumvar::tag<Shape::rect>, Rect{1.5, 2.0});
  if (v.index() == Shape::rect) {
    auto& r = asf_enumvar::get<Shape::rect>(v);
    r.w *= 2.0;
  }
  std::println("doubled width: area = {}", area(v));

  // replace the payload in place; the tag follows
  v.emplace<Shape::circle>(Circle{1.0});
  std::println("after emplace: {} (area = {})", name_of(v.index()), area(v));
}
