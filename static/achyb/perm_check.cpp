#include "color.h"
#include "perm_check.h"

#include "llvm/IR/CallSite.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/CFG.h"

#include "utility.h"

#include <fstream>


void PermCheck::load_ac_interfaces(std::string &file) {
  // errs() << "load_ac_interfaces started" << "\n";

  std::ifstream input(file);

  if (!input.is_open()) {
    errs() << "Yang: missing ac interface file:" << "\n";
    exit(0);
  }

  // Yang: read interface file
  std::string line;
  while (std::getline(input, line)) {
    errs() << line << "\n";
    name_set.insert(line);
  }
  input.close();
  // errs() << "Load access control interfaces from file: " << name_set.size() << "\n";

}

bool PermCheck::is_wrap_equal(Function* f1, Function* f2) {
  int depth = 5;

  if(f1 == f2) {
  	return true;
  }

  FunctionSet f1_wrapper_set, f2_wrapper_set;
  get_wrapper_tree(f1_wrapper_set, f1, depth);
  // Debug
  
	//errs() << "wrappers of " << f1->getName() << ":\n";
	//for(auto f1_wrapper : f1_wrapper_set) {
	//	errs() << f1_wrapper->getName() << "\n";
	//}
  

  /*errs() << f1->getName() << ":\n";
  for(auto f1_wrapper : f1_wrapper_set) {
    errs() << f1_wrapper->getName() << "\n";
  }*/

  get_wrapper_tree(f2_wrapper_set, f2, depth);

  bool is_equ = false;
  for(auto f1_wrapper : f1_wrapper_set) {
    if(f2_wrapper_set.find(f1_wrapper) != f2_wrapper_set.end()) {
    	if(f1_wrapper->getReturnType()->isIntegerTy()) {
    		is_equ = true;
      		break;
    	}
      // errs() << "common wrapper: " << f1_wrapper->getName() << "\n";
    }
  }

  return is_equ;
}


void PermCheck::get_wrapper_tree(FunctionSet& fs, Function* f, int depth) {
  if(depth > 0) {
    fs.insert(f);

    for (Function::iterator fi = f->begin(), fe = f->end(); fi != fe; ++fi) {
        BasicBlock *bb = dyn_cast<BasicBlock>(fi);
        for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
          CallInst *ci = dyn_cast<CallInst>(ii);
          if (!ci) {
            continue;
          }

          // temporariliy only consider direct calls
          Function* child = get_callee_function_direct(ci);
          if (!child) {
            continue;
          }

          if(block_list.find(child->getName()) == block_list.end()) {
            get_wrapper_tree(fs, child, depth - 1);
          }

      }
    }
  }

}

PermCheck::PermCheck(Module &module, std::string &capfile, bool sa)
    : GatingFunctionBase(module), semiauto(sa) {
  // errs() << "Access control mechanism: capability\n";
  
  // std::string a = "printk";
  block_list.insert("printk");
  block_list.insert("llvm.dbg.value");

  load_ac_interfaces(capfile);

  //
  // Yang: get function and remove other return types
  for (Module::iterator fi = module.begin(); fi != module.end(); ++fi) {
    Function *func = dyn_cast<Function>(fi);

    // Yang: get return type
    Type* ret_type = func->getReturnType();
    if(!(ret_type->isIntegerTy())) { // Yang: must return integer
      continue;
    }

    StringRef fname = func->getName();

    if (name_set.find(fname) != name_set.end()) {
      pointer_set.insert(func);
    }
  }
  // errs() << "Function IR: " << pointer_set.size() << "\n";

  // Yang:
  if (!semiauto) { 
  	perm_check_set.insert(pointer_set.begin(), pointer_set.end());
  	// errs() << "Permision checks loaded " << perm_check_set.size() << "\n";
  	return;
  }

  // Yang: equivalence relation consturction
  EquRelation equ_rel;
  for(auto f1 : pointer_set) {
    if(equ_rel.size() == 0) {
        FunctionSet equ_class;
        equ_class.insert(f1);
        equ_rel.push_back(equ_class);
    } else {
      bool is_process = false;
      for(auto& equ_class: equ_rel) {
        auto f2 = *(equ_class.begin());
        if(is_wrap_equal(f1, f2)) {
          equ_class.insert(f1);
          is_process = true;
          break;
        }
      }

      if(!is_process) {
        FunctionSet equ_class;
        equ_class.insert(f1);
        equ_rel.push_back(equ_class);
      }
    }
  }

  // Debug
  errs() << "equ_class_num=" << equ_rel.size() << "\n";
  for(auto& equ_class: equ_rel) {
  	auto f2 = *(equ_class.begin());
  	errs() << f2->getName() << " " << equ_class.size() << "\n";
  	for(auto f3 : equ_class) {
  		errs() << f3-> getName() << "\n";
  	}
  	errs() << "\n";
  }


  for(auto& equ_class : equ_rel) {
    auto f = *(equ_class.begin());

    char choice = 'A';
    while(choice != 'Y' && choice != 'N') { // get user input
      errs()<< "Function Name: " << f->getName() << " (Y/N)\n";
      scanf(" %c", &choice);
    }

    if(choice == 'Y') {
      perm_check_set.insert(equ_class.begin(), equ_class.end());
    }
  }

  errs() << "perm_check_num=" << perm_check_set.size() << "\n";
  for(auto perm_check : perm_check_set) {
    errs() << perm_check->getName() << "\n";
  }
}


bool PermCheck::is_gating_function(Function *f) {
  return perm_check_set.find(f) != perm_check_set.end();
}

bool PermCheck::is_gating_function(std::string &str) {
  for (auto f : perm_check_set) {
    if (f->getName() == str)
      return true;
  }
  return false;
}

void PermCheck::dump() {
  errs() << ANSI_COLOR(BG_BLUE, FG_WHITE)
         << "perm_check_num="
         << perm_check_set.size() << ANSI_COLOR_RESET << "\n";
  for (auto f :perm_check_set) {
    errs() << f->getName() << "\n";
  }
  errs() << "=o=\n";
}