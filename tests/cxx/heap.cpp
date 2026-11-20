// Dynamic memory in C++: new and delete on the C library's malloc, and the
// standard library's owners of it. Reports through LATD: 0x5A, or the
// number of the first failure. For scripts/test-cxx.sh.
#include <xc.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

static uint8_t failed;
static void check(bool ok, uint8_t number)
{
    if (!ok && !failed)
        failed = number;
}

static uint8_t alive;
struct Node {
    uint8_t value;
    Node *next;
    Node(uint8_t v, Node *n) : value(v), next(n) { alive++; }
    virtual ~Node() { alive--; }
};
struct Marked : Node {
    using Node::Node;
    ~Marked() override { alive -= 10; }
};

volatile uint8_t seed = 5;

int main()
{
    // new and delete, through a virtual destructor
    Node *list = nullptr;
    for (uint8_t i = 0; i < seed; i++)
        list = new Node(i, list);
    check(alive == 5 && list->value == 4 && list->next->next->value == 2, 1);
    while (list) {
        Node *next = list->next;
        delete list;
        list = next;
    }
    check(alive == 0, 2);
    alive = 20;
    Node *m = new Marked(1, nullptr);
    delete m;
    check(alive == 10, 3);
    int *block = new int[seed];
    block[4] = 1234;
    check(block[4] == 1234, 4);
    delete[] block;

    // std::unique_ptr
    alive = 0;
    {
        auto p = std::make_unique<Node>(7, nullptr);
        std::unique_ptr<Node> q = std::move(p);
        check(!p && q && q->value == 7 && alive == 1, 10);
    }
    check(alive == 0, 11);

    // std::vector
    std::vector<uint16_t> v;
    for (uint16_t i = 0; i < 20; i++)
        v.push_back(i * i);
    check(v.size() == 20 && v[19] == 361 && v.capacity() >= 20, 20);
    v.erase(v.begin(), v.begin() + 10);
    check(v.size() == 10 && v.front() == 100, 21);
    uint16_t sum = 0;
    for (auto x : v)
        sum += x;
    check(sum == 2185, 22);

    // std::string
    std::string s = "pic";
    s += "16f";
    s += std::to_string(1900 + seed * 7 + 2);
    check(s == "pic16f1937" && s.size() == 10 && s.find("16") == 3, 30);

    // std::function
    std::function<uint8_t(uint8_t)> f = [](uint8_t x) { return uint8_t(x + 1); };
    uint8_t k = seed;
    check(f(1) == 2, 40);
    f = [k](uint8_t x) { return uint8_t(x * k); };
    check(f(3) == 15, 41);

    LATD = failed ? failed : 0x5A;
    SLEEP();
    return 0;
}
