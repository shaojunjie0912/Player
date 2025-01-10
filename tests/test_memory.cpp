#include <iostream>
#include <memory>

using std::cout;
using std::endl;

struct A {
    int value{1024};
};

int main() {
    auto delete_a = [](A* x) {
        cout << "Deleting A with value: " << x->value << endl;
        delete x;
    };
    std::unique_ptr<A, decltype(delete_a)> pa{new A{42}, delete_a};
    // std::shared_ptr<A> sp_a{new A{42}};
    // sp_a = nullptr;
    return 0;
}