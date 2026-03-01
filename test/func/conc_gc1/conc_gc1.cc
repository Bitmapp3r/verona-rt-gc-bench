// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cpp/when.h>
#include <debug/harness.h>
#include <verona.h>


struct Node : public V<Node> {
    Node(int id) : id(id) {}
    int id;
    Node* next;
    ~Node() {Logging::cout() << "node " << id << " died\n";}
    void trace(ObjectStack& st) {
        if (!next)
            st.push(next);
    }
};

class Reg : public V<Reg> {
public:

    void trace(ObjectStack& st) {
        if (root) 
            st.push(root);
        
        if (next)
            st.push(next);
    }
    void finaliser(Object* region, ObjectStack& sub_regions) {
        if (region) {
            Object::add_sub_region(next, region, sub_regions);
        }
    }
    Reg* next;
    Node* root;
};

class RegionOwner {
public:
    Reg* reg;
    RegionOwner() {
        reg = new (RegionType::Rc) Reg;
        Reg* sub_reg = new (RegionType::Trace) Reg;
        reg->next = sub_reg;
        Logging::cout() << (void*)reg << " (" << reg << ")" <<  
            " region points to region " 
                        << (void*)sub_reg << " (" << sub_reg << ")\n";
    }
    ~RegionOwner() {
        Logging::cout() << "Cown Dying\n";
        
        region_release(reg);
        
    }
};
using namespace verona::cpp;

void test() {
    auto cown = make_cown<RegionOwner>(); 
    when(cown) << [&](auto c) {
        Logging::cout() << "hello...?\n";
        //schedule_gc(c->reg);
        UsingRegion(c->reg);
        UsingRegion(c->reg->next);
    };
    
    when(cown) << [&](auto c) {
        UsingRegion rr(c->reg->next);
    };
    
    //Cown::acquire(cown);
    Logging::cout() << "finished?\n";
}

int main(int argc, char** argv)
{
  SystematicTestHarness harness(argc, argv);
  Logging::enable_logging();

  harness.run(test);

  return 0;
}
