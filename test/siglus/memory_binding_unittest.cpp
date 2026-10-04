// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
// -----------------------------------------------------------------------

#include <gtest/gtest.h>

#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "m6/vm_factory.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace libsiglus::binding {

class SiglusMemoryBindingTest : public ::testing::Test {
 protected:
  SiglusRuntime runtime;

  void SetUp() override {
    runtime.vm = std::make_unique<serilang::VM>(m6::VMFactory::Create());

    const auto* bind = SiglusBindingRegistry::Find("0_memory");
    ASSERT_NE(bind, nullptr);
    (*bind)(runtime);
  }

  serilang::Value Eval(std::string src) {
    return Execute(*runtime.vm, std::move(src));
  }
};

TEST_F(SiglusMemoryBindingTest, IntBanksReadWriteAndGrow) {
  Eval("A[0] = 7;");
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 7);

  Eval("A[2500] = 9;");
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 2500), 9);
}

TEST_F(SiglusMemoryBindingTest, IntBankBitViewsShareBackingStorage) {
  Eval("B[0] = 0; B.write_b1(3, 1); A[0] = B.b1(3);");
  EXPECT_EQ(runtime.memory->Read(IntBank::B, 0), 8);
  EXPECT_EQ(runtime.memory->Read(IntMemoryLocation(IntBank::B, 3, 1)), 1);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 1);
}

TEST_F(SiglusMemoryBindingTest, StringBanksReadWriteAndNameBanksWork) {
  Eval(R"(S[0] = "x";)");
  Eval(R"(LN[1] = "local";)");
  Eval(R"(GN[1] = "global";)");

  EXPECT_EQ(runtime.memory->Read(StrBank::S, 0), "x");
  EXPECT_EQ(runtime.memory->Read(StrBank::local_name, 1), "local");
  EXPECT_EQ(runtime.memory->Read(StrBank::global_name, 1), "global");
}

TEST_F(SiglusMemoryBindingTest, BankOperationsRemainCallable) {
  Eval("A.resize(3);");
  EXPECT_EQ(runtime.memory->Size(IntBank::A), 3);

  Eval("A.fill(0, 3, 5); A.Set(1, 11, 12);");
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 1), 11);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 2), 12);
}

TEST_F(SiglusMemoryBindingTest, FactoryIntListsUseSiglusFacade) {
  Eval("xs = make_intlist(4); xs.Set(1, 10, 20); A[0] = xs[1]; A[1] = xs[2];");
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 10);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 1), 20);

  Eval("xs[0] = 0; xs.write_b2(1, 3); A[2] = xs.b2(1); A[3] = xs[0];");
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 2), 3);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 3), 12);

  Eval("xs.resize(5); xs[4] = 7; xs.init(); A[4] = xs.size(); A[5] = xs[1];");
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 4), 4);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 5), 0);
}

TEST_F(SiglusMemoryBindingTest, FactoryIntListBitViewsGrowFromEmpty) {
  Eval(R"(
xs = make_intlist(0);
A[0] = xs.b4(3);
A[1] = xs.size();
xs.write_b8(4, 17);
A[2] = xs.size();
A[3] = xs.b8(4);
A[4] = xs[1];
)");

  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 0);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 1), 1);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 2), 2);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 3), 17);
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 4), 17);
}

TEST_F(SiglusMemoryBindingTest, FactoryStrListsUseSiglusFacade) {
  Eval(
      R"(ss = make_strlist(2); ss[0] = "x"; ss.resize(3); ss[2] = "z"; A[0] = ss.size(); S[0] = ss[0]; S[1] = ss[2];)");
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 3);
  EXPECT_EQ(runtime.memory->Read(StrBank::S, 0), "x");
  EXPECT_EQ(runtime.memory->Read(StrBank::S, 1), "z");

  Eval("ss.init(); A[1] = ss.size(); S[2] = ss[0];");
  EXPECT_EQ(runtime.memory->Read(IntBank::A, 1), 2);
  EXPECT_EQ(runtime.memory->Read(StrBank::S, 2), "");
}

TEST_F(SiglusMemoryBindingTest, FactoriesResolveClassesFromBootstrapBuiltins) {
  serilang::VM bootstrap_vm(runtime.vm->gc_);
  bootstrap_vm.gc_threshold_ = 0;

  auto bootstrap_builtins =
      std::make_shared<std::unordered_map<std::string, serilang::Value>>(
          *runtime.vm->globals_);
  bootstrap_builtins->insert(runtime.vm->builtins_->begin(),
                             runtime.vm->builtins_->end());
  bootstrap_vm.builtins_ = std::move(bootstrap_builtins);

  Execute(bootstrap_vm, R"(
xs = make_intlist(3);
xs[1] = 42;
A[0] = xs[1];
ss = make_strlist(2);
ss[1] = "boot";
S[0] = ss[1];
)");

  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 42);
  EXPECT_EQ(runtime.memory->Read(StrBank::S, 0), "boot");
}

TEST_F(SiglusMemoryBindingTest, PushAndPopFrameRestoresStackBanks) {
  Eval(R"(
L[0] = 42;
K[0] = "old";
)");
  EXPECT_EQ(runtime.memory->Read(IntBank::L, 0), 42);
  EXPECT_EQ(runtime.memory->Read(StrBank::K, 0), "old");

  Eval(R"(
__builtin_push_frame([1, 2], ["arg"]);
L[0] = 99;
K[1] = "new";
)");
  EXPECT_EQ(runtime.memory->Read(IntBank::L, 0), 99);
  EXPECT_EQ(runtime.memory->Read(IntBank::L, 1), 2);
  EXPECT_EQ(runtime.memory->Read(StrBank::K, 0), "arg");
  EXPECT_EQ(runtime.memory->Read(StrBank::K, 1), "new");

  Eval("__builtin_pop_frame();");
  EXPECT_EQ(runtime.memory->Read(IntBank::L, 0), 42);
  EXPECT_EQ(runtime.memory->Read(StrBank::K, 0), "old");
}

TEST_F(SiglusMemoryBindingTest, RuntimeResetClearsLocalAndStackBanksOnly) {
  Eval(R"(
A[0] = 11;
S[0] = "local";
G[0] = 22;
M[0] = "global";
L[0] = 33;
K[0] = "stack";
)");

  for (auto& cb : runtime.reset_local_memory)
    cb();

  EXPECT_EQ(runtime.memory->Read(IntBank::A, 0), 0);
  EXPECT_EQ(runtime.memory->Read(StrBank::S, 0), "");
  EXPECT_EQ(runtime.memory->Read(IntBank::L, 0), 0);
  EXPECT_EQ(runtime.memory->Read(StrBank::K, 0), "");
  EXPECT_EQ(runtime.memory->Read(IntBank::G, 0), 22);
  EXPECT_EQ(runtime.memory->Read(StrBank::M, 0), "global");
}

}  // namespace libsiglus::binding
