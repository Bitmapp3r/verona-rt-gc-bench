

#include <debug/harness.h>
#include <cpp/when.h>

class Account {
public:
   int balance = 100;
   void inc() {
     balance += 10;   
   }
   void dec() {
      balance -= 10; 
      assert(balance != 90); 
   }
   
};

using namespace verona::cpp;

void body()
{
  Logging::cout() << "did something" << Logging::endl;  
auto src = make_cown<Account>();
  auto dst = make_cown<Account>();
  when (src) << [=](Account& s) {
    s.inc();
    Logging::cout() << "src(inccing) has balance: '" << s.balance << "'" << Logging::endl;
  };
  when(src) << [=](Account& s) {
    s.dec(); 
    Logging::cout() << "src(deccing) has balance: '" << s.balance << "'" <<  Logging::endl;
   };

}


int main(int argc, char** argv) {

   SystematicTestHarness harness(argc, argv);
   Logging::enable_logging();
   harness.run(body);

}
