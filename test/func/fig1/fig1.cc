#include <cpp/when.h>
#include <debug/harness.h>
#include <iostream>
#include <region/region.h>
#include <object/object.h>
#include <cpp/vobject.h>

class Node : public V<Node> {
public:
    int id;
    Node* next;
    Node(int id, Node* next) : id(id), next(next) {}

    void trace(ObjectStack &os) const {
        if (next != nullptr) {
            os.push(next);
        }
    }
    ~Node() {
        Logging::cout() << "node " << id << " died..." << Logging::endl;
    }
};


using namespace verona::cpp;
using namespace verona::rt;

void create_fig1() {

    auto a = make_cown<Node>(0, nullptr);
    when (a) << [=](auto r) {
        // opening region R
        // r is an acquiredCown<Node> not a Node...
        auto e = new (RegionType::Trace) Node(1, &r.get_ref());
        // is this allocated in the same region as a?
        auto c = new (RegionType::Trace) Node(2, e);
        r->next = c;

        // operator new not accessible in this context
    };
}

/*
object.h:282:
verona::rt::Object::Object(): Assertion `last_alloc(nullptr) == this' failed.

*/


int main(int argc, char** argv) {
    SystematicTestHarness harness(argc, argv);
    Logging::enable_logging();
    harness.run(create_fig1);
    
}