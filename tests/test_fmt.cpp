#include <fmt/core.h>
#include <fmt/ranges.h>

#include <array>
#include <vector>

using fmt::print;

int main() {
    fmt::print("Hello, World!\n");
    fmt::print(stderr, "error: {}\n", 404);
    std::vector<int> v{1, 2, 3};
    print("v: {}\n", v);
    std::array<double, 4> a{1.0, 2.0, 3.0, 4.0};
    print("a: {}\n", a);
    print("{}\n", fmt::join(v, "----"));
    int x{1};
    // print("{1}\n", x);
    return 0;
}