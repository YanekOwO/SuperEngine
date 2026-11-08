// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 RLVM contributors
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

#include "libsiglus/native_operation_serialization.hpp"
#include "libsiglus/archive.hpp"
#include "libsiglus/execution_serialization.hpp"
#include "libsiglus/native_wait_serialization.hpp"
#include "libsiglus/siglus_runtime.hpp"
#include "long_operations/siglus_textout.hpp"
#include "long_operations/siglus_wait.hpp"
#include "serialization_test_helpers.hpp"
#include "vm/future.hpp"
#include "vm/promise.hpp"

#include <gtest/gtest.h>

namespace libsiglus {
namespace {
namespace sr = serilang;

class SiglusNativeOperationSerializationTest : public ::testing::Test {
 protected:
  void Initialize(SiglusRuntime& runtime) {
    runtime.archive = std::make_shared<Archive>(
        std::string(sizeof(Pack_hdr), '\0'), xorkey_t{});
    runtime.binding_names = {"0_memory", "flow"};
    runtime.GameplayMachine().Initialize();
  }
  void SetUp() override {
    Initialize(source);
    Initialize(target);
  }
  SiglusRuntime source, target;
};

TEST_F(SiglusNativeOperationSerializationTest,
       PreparedLazyOperationWaitsForAwait) {
  // A suspended task that was never awaited remains inert after activation.
  auto& original = source.GameplayMachine();
  lop::FutureBackedCoroutineTask task(
      *original.vm,
      std::make_unique<lop::SiglusTextoutOperation>(nullptr, nullptr, "text"),
      nullptr, {}, original.WaitScope());
  auto future = sr::Value(task.MakeFuture(*original.vm->gc_));
  ExecutionWriter writer;
  const auto id = writer.PromiseId(sr::get_promise(future));
  auto waits = serialization_test::RoundTripSnapshot(
      CaptureNativeWaits(original, writer), source, target);
  ExecutionReader reader;
  PrepareNativeWaits(target.GameplayMachine(), reader, waits);
  auto completion = reader.Promise(id);
  ActivateNativeWaits(target.GameplayMachine());
  EXPECT_FALSE(completion->HasResult());
  sr::Value awaiter, awaited;
  completion->initial_await(*target.GameplayMachine().vm, awaiter, awaited);
  ASSERT_TRUE(completion->HasResult());
  EXPECT_EQ(completion->result->value(), sr::Value(0));
}

TEST_F(SiglusNativeOperationSerializationTest,
       OpaqueNativeOperationIsRejected) {
  // A native task without a logical codec cannot produce a misleading save.
  class OpaqueOperation : public lop::LongOperation {
   public:
    Task<int> Run(lop::WaitContext&) override { co_return 0; }
  } operation;
  ExecutionWriter writer;
  EXPECT_THROW(CaptureNativeOperation(operation, writer), std::runtime_error);
}

}  // namespace
}  // namespace libsiglus
