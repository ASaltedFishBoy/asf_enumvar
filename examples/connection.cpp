// asf_enumvar example -- tuple mode: alternatives are listed apart from
// the enum, in declaration order; std::monostate marks a fieldless
// position. A state machine is the classic use.
//
// Examples are narrative demos meant for reading; correctness checks
// (assertions, edge cases, static_assert batteries) live in tests/.

#include <enumvar.hpp>

#include <format>
#include <print>
#include <string>
#include <utility>

struct Session {
  int fd;
  std::string peer;
};

// closed carries nothing; listening carries a port; established carries
// a Session. The tuple spells the alternatives, not annotations.
enum class Conn { closed, listening, established };
using ConnVar =
    asf_enumvar::var<Conn, std::tuple<std::monostate, unsigned short, Session>>;

std::string describe(const ConnVar& v) {
  return asf_enumvar::match(
      v,
      // a fieldless arm may be nullary
      asf_enumvar::on<Conn::closed>([] { return std::string{"closed"}; }),
      asf_enumvar::on<Conn::listening>(
          [](unsigned short port) {
            return std::format("listening on :{}", port);
          }),
      asf_enumvar::on<Conn::established>([](const Session& s) {
        return std::format("established: fd {}, peer {}", s.fd, s.peer);
      }));
}

// Transitions dispatch on the CURRENT ENUMERATOR -- index() returns
// Conn, not a position number.
void listen(ConnVar& v, unsigned short port) {
  if (v.index() != Conn::closed) {
    std::println("listen: ignored (not closed)");
    return;
  }
  v.emplace<Conn::listening>(port);
}

void accept(ConnVar& v, Session s) {
  if (v.index() != Conn::listening) {
    std::println("accept: ignored (not listening)");
    return;
  }
  v.emplace<Conn::established>(std::move(s));
}

void close(ConnVar& v) { v.emplace<Conn::closed>(); }

int main() {
  ConnVar v;  // first alternative: closed
  std::println("{}", describe(v));

  listen(v, 8080);
  std::println("{}", describe(v));

  accept(v, Session{7, "192.0.2.1"});
  std::println("{}", describe(v));

  // enum-keyed get_if: nullptr unless that enumerator is active
  if (auto* s = asf_enumvar::get_if<Conn::established>(&v))
    std::println("peer seen through get_if: {}", s->peer);

  listen(v, 9090);  // ignored: not closed
  close(v);
  std::println("{}", describe(v));
}
