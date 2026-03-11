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
        if (next)
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
        reg = new (RegionType::Arena) Reg;
        Reg* sub_reg = new (RegionType::Rc) Reg;
        reg->next = sub_reg;
        std::cout << (void*)reg << " (" << reg << ")" <<  
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
        {
           UsingRegion rr(c->reg);
            //std::cout << ((Object*)(c->reg))->is_opened() << "\n";  
            c->reg->root = new Node(0);
            c->reg->root->next = new Node(1);
        }
        {
            UsingRegion rr(c->reg->next);
            c->reg->next->root = new Node(2);
            c->reg->next->root->next = new Node(3);
        }
    };
    for (int i = 0; i < 50; i++) {
        when(cown) << [=](auto c) {
            Logging::cout() << "second region start " << i << "\n";
            {
                UsingRegion rr(c->reg->next);
                Node* temp = c->reg->next->root->next;
                Node* old_root = c->reg->next->root;
                temp->next = old_root;
                old_root->next = nullptr;
                c->reg->next->root = temp;
                for (volatile int j = 0; j < 1000000; j++) {
                    c->reg->next->root->id = j;
                    snmalloc::Aal::pause();
                }
                yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            Logging::cout() << "second region end " << i << "\n";
        };
    }
    
    //Cown::acquire(cown);
    Logging::cout() << "finished?\n";
}

int main(int argc, char** argv)
{
    
  Logging::enable_logging();
  size_t cores = 6;
  Scheduler& sched = Scheduler::get();
  sched.init(cores);
  sched.set_fair(false);

  test();
  sched.run();
  //  test();
  puts("done");


  return 0;
}
