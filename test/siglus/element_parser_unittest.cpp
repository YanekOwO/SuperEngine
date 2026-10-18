// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <format>

#include "libsiglus/element.hpp"
#include "libsiglus/element_parser.hpp"
#include "utilities/mpl.hpp"

namespace siglus_test {
using namespace libsiglus::elm;
using namespace libsiglus;

using ::testing::ElementsAre;
using ::testing::HasSubstr;

class ElementParserTest : public ::testing::Test {
 protected:
  ElementParserTest() { ResetParser(); }

  void ResetParser() {
    parser = std::make_unique<ElementParser>(
        scene_properties, global_properties, scene_commands, global_commands,
        curcall_args, scene_id, [this] { return ReadKidoku(); },
        [this](std::string message) { Warn(std::move(message)); });
  }

  void SetKidoku(std::initializer_list<int> values) {
    kidoku_values.assign(values.begin(), values.end());
    kidoku_idx = 0;
  }

  int ReadKidoku() {
    if (kidoku_idx >= kidoku_values.size()) {
      ADD_FAILURE() << "unexpected kidoku read";
      return 0;
    }
    return kidoku_values[kidoku_idx++];
  }

  void Warn(std::string message) {
    warnings.emplace_back(std::move(message));
    if (fail_on_warn)
      ADD_FAILURE() << warnings.back();
  }

  std::vector<Property> scene_properties;
  std::vector<Property> global_properties;
  std::vector<Command> scene_commands;
  std::vector<Command> global_commands;
  std::vector<Type> curcall_args;
  std::vector<int> kidoku_values;
  size_t kidoku_idx = 0;
  int scene_id = 0;
  bool fail_on_warn = true;
  std::vector<std::string> warnings;
  std::unique_ptr<ElementParser> parser;

  // ==============================================================================
  // Test helpers
  struct ChainCtx {
    AccessChain chain;

    bool operator==(std::string rhs) const {
      return chain.ToDebugString() == rhs;
    }

    friend std::ostream& operator<<(std::ostream& os, const ChainCtx& ctx) {
      return os << ctx.chain.ToDebugString();
    }

    // [overload id] (empty for no overload), a = await result, s = simple call.
    std::string CallFlags() const {
      const Call* call = chain.nodes.empty()
                             ? nullptr
                             : std::get_if<Call>(&chain.nodes.back().var);
      if (!call)
        return "not a call: " + chain.ToDebugString();

      std::string flags;
      flags += std::format("[{}]", call->overload_id
                                       ? std::to_string(*call->overload_id)
                                       : std::string());
      if (call->await_result)
        flags += 'a';
      if (call->is_simple)
        flags += 's';

      return flags;
    }
  };

  ChainCtx chain(ElementCode elm) {
    return ChainCtx{.chain = parser->Parse(elm)};
  }
  template <typename... Ts>
    requires(std::same_as<Ts, int> && ...)
  ChainCtx chain(Ts&&... elms) {
    ElementCode elmcode{std::forward<Ts>(elms)...};
    return ChainCtx{.chain = parser->Parse(elmcode)};
  }

  template <typename T>
  inline static Value v(T param) {
    if constexpr (std::same_as<T, int>)
      return Value(Integer(param));
    else if constexpr (std::constructible_from<std::string, T>)
      return Value(String(std::move(param)));
    else
      static_assert(always_false<T>);
  }

  inline static Value list(std::initializer_list<Value> values) {
    return Value(List{std::vector<Value>(values)});
  }
};

TEST_F(ElementParserTest, MemoryBank) {
  EXPECT_EQ(chain(25, -1, 0), "A[int:0]");
  EXPECT_EQ(chain(26, 3, -1, 1), "B.b1[int:1]");
  EXPECT_EQ(chain(27, 4, -1, 2), "C.b2[int:2]");
  EXPECT_EQ(chain(28, 5, -1, 3), "D.b4[int:3]");
  EXPECT_EQ(chain(29, 7, -1, 4), "E.b8[int:4]");
  EXPECT_EQ(chain(30, 6, -1, 5), "F.b16[int:5]");
  EXPECT_EQ(chain(31, -1, 250), "G[int:250]");
  EXPECT_EQ(chain(32, -1, 251), "Z[int:251]");
  EXPECT_EQ(chain(34, 3), "S.init()");
}

TEST_F(ElementParserTest, SelectionCommandsKeepReadFlagsAndResults) {
  for (const auto& [id, name] :
       std::vector<std::pair<int, std::string>>{{19, "sel"},
                                                {101, "sel_cancel"},
                                                {100, "selmsg"},
                                                {102, "selmsg_cancel"},
                                                {76, "selbtn"},
                                                {77, "selbtn_cancel"},
                                                {127, "selbtn_start"}}) {
    SCOPED_TRACE(id);
    SetKidoku({42});
    ElementCode code{id};
    code.ForceBind({0, {v("yes"), v("no")}, Type::Int});
    const auto parsed = chain(code);
    EXPECT_THAT(parsed.chain.ToDebugString(), HasSubstr(name));
    EXPECT_EQ(parsed.chain.kidoku, 42);
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
    ASSERT_FALSE(parsed.chain.nodes.empty());
    const auto* call = std::get_if<Call>(&parsed.chain.nodes.back().var);
    ASSERT_NE(call, nullptr);
    EXPECT_TRUE(call->await_result);
    EXPECT_EQ(kidoku_idx, 1);
  }
}

TEST_F(ElementParserTest, PreparedSelectionsAndLastTextDoNotReadTrailers) {
  for (const auto& [id, name] :
       std::vector<std::pair<int, std::string>>{{126, "selbtn_ready"},
                                                {128, "selbtn_cancel_ready"},
                                                {162, "get_last_sel_msg"}}) {
    SCOPED_TRACE(id);
    ElementCode code{id};
    code.ForceBind({0, {}, id == 162 ? Type::String : Type::None});
    const auto parsed = chain(code);
    EXPECT_THAT(parsed.chain.ToDebugString(), HasSubstr(name));
    EXPECT_FALSE(parsed.chain.kidoku.has_value());
    ASSERT_FALSE(parsed.chain.nodes.empty());
    const auto* call = std::get_if<Call>(&parsed.chain.nodes.back().var);
    ASSERT_NE(call, nullptr);
    EXPECT_FALSE(call->await_result);
  }
}

TEST_F(ElementParserTest, WindowSelectionsConsumeExactlyOneReadFlag) {
  for (int id : {5, 51, 50, 52}) {
    SCOPED_TRACE(id);
    SetKidoku({99});
    ElementCode code{38, 3, -1, 1, id};
    code.ForceBind({0, {v("choice")}, Type::Int});
    const auto parsed = chain(code);
    EXPECT_EQ(parsed.chain.kidoku, 99);
    EXPECT_EQ(kidoku_idx, 1);
    EXPECT_THAT(parsed.CallFlags(), HasSubstr("a"));
  }
}

TEST_F(ElementParserTest, PropertyBindsRejectInvalidArgumentsAndOverloads) {
  for (const auto& source :
       {ElementCode{38, 2, -1, 0, 3}, ElementCode{79, 0}}) {
    for (const auto& bind :
         {Invoke{0, {v(1)}}, Invoke{1, {}}, Invoke{1, {v(1), v(2)}},
          Invoke{2, {}}, Invoke{-1, {}}}) {
      ElementCode elm = source;
      elm.ForceBind(bind);
      EXPECT_THROW(chain(elm), std::runtime_error);
    }
    for (int overload : {0, 1}) {
      Invoke bind{overload, overload == 0 ? std::vector<Value>{}
                                          : std::vector<Value>{v(1)}};
      bind.named_arg.emplace_back(7, v(2));
      ElementCode elm = source;
      elm.ForceBind(std::move(bind));
      EXPECT_THROW(chain(elm), std::runtime_error);
    }
  }
}

TEST_F(ElementParserTest, PropertyPrefixesPreserveFinalCommandBinds) {
  ElementCode elm{38, 2, -1, 0, 93, -1, 1, 120};
  Invoke bind{1, {v("movie"), v(0), v(0), v(640), v(480)}};
  bind.named_arg.emplace_back(4, v(1));
  elm.ForceBind(bind);
  auto parsed = chain(elm);
  EXPECT_EQ(parsed,
            "stage.front.object[int:0].child[int:1].create_movie[1](str:movie,"
            "int:0,int:0,int:640,int:480,4=int:1)");
  EXPECT_EQ(parsed.CallFlags(), "[1]");
}

TEST_F(ElementParserTest, BitPropertiesKeepDynamicIndicesAndValidateMarkers) {
  ElementCode dynamic{26, 3, -1, 0};
  dynamic.code.back() = Variable{Type::Int, 7};
  auto parsed = chain(dynamic);
  EXPECT_EQ(parsed, "B.b1[t7]");

  fail_on_warn = false;
  auto missing = chain(26, 3);
  EXPECT_EQ(missing, "B.b1[int:-1]");
  EXPECT_THAT(
      warnings,
      ElementsAre(HasSubstr("[IntListBit] b1 expected immediate index")));
  warnings.clear();
  auto invalid = chain(26, 3, 0, 1);
  EXPECT_EQ(invalid, "B.b1[int:-1]");
  EXPECT_THAT(warnings, ::testing::Contains(HasSubstr(
                            "[IntListBit] b1 expected immediate index")));
}

TEST_F(ElementParserTest, Farcall) {
  {
    ElementCode elm{5};
    elm.ForceBind({0, {v("scnname")}});
    EXPECT_EQ(chain(elm), "farcall@[str:scnname].z[int:0]()()");
  }
  {
    ElementCode elm{5};
    elm.ForceBind({1, {v("name"), v(1), v(2), v("3"), v(4)}});
    EXPECT_EQ(chain(elm), "farcall@[str:name].z[int:1](int:2,int:4)(str:3)");
  }
  {
    // dynamic farcall
    ElementCode elm{5};
    elm.ForceBind({1,
                   {Value(Variable(Type::String, 123)),
                    Value(Variable(Type::Int, 456))}});
    EXPECT_EQ(chain(elm), "farcall@[t123].z[t456]()()");
  }
}

TEST_F(ElementParserTest, Jump) {
  {
    ElementCode elm{4};
    elm.ForceBind({0, {v("scene_name")}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "jump(str:scene_name)");
    EXPECT_EQ(parsed.chain.GetType(), Type::None);
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{4};
    elm.ForceBind({0, {v(69), v(2)}});
    EXPECT_EQ(chain(elm), "jump(int:69,int:2)");
  }
}

TEST_F(ElementParserTest, TimeWait) {
  {
    ElementCode elm{54};
    elm.ForceBind({0, {v(123)}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "wait(int:123)");
    EXPECT_EQ(parsed.chain.GetType(), Type::None);
    EXPECT_EQ(parsed.CallFlags(), "[]as");
  }
  {
    ElementCode elm{55};
    elm.ForceBind({0, {v(456)}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "wait_key(int:456)");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
    EXPECT_EQ(parsed.CallFlags(), "[]as");
  }
  {
    ElementCode elm{55};
    elm.ForceBind({0, {Value(Variable(Type::Int, 456))}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "wait_key(t456)");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
    EXPECT_EQ(parsed.CallFlags(), "[]as");
  }
}

TEST_F(ElementParserTest, Title) {
  {
    ElementCode elm{74};
    elm.ForceBind({0, {v("title")}});
    EXPECT_EQ(chain(elm), "set_title(str:title)");
  }
  {
    ElementCode elm{75};
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "get_title()");
    EXPECT_EQ(parsed.chain.GetType(), Type::String);
  }
}

TEST_F(ElementParserTest, FrameAction) {
  {
    ElementCode elm{53, -1, 8, 1};
    elm.ForceBind({0, {v(-1), v("$$command_name")}});
    ChainCtx parsed = chain(elm);
    EXPECT_EQ(parsed,
              "frame_action_ch[int:8].start(int:-1,str:$$command_name)");
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{79, 3};
    elm.ForceBind({0, {v(-1), v("$$command_name")}});
    ChainCtx parsed = chain(elm);
    EXPECT_EQ(parsed, "frame_action.start_real(int:-1,str:$$command_name)");
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{79, 1};
    elm.ForceBind({0,
                   {Value(Variable(Type::Int, 456)),
                    Value(Variable(Type::String, 123))}});
    ChainCtx parsed = chain(elm);
    EXPECT_EQ(parsed, "frame_action.start(t456,t123)");
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{79, 2};
    elm.ForceBind({0, {}});
    ChainCtx parsed = chain(elm);
    EXPECT_EQ(parsed, "frame_action.end()");
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
}

TEST_F(ElementParserTest, FrameActionCounterGetterReturnsCounter) {
  auto parsed = chain(79, 0);

  EXPECT_EQ(parsed, "frame_action.counter");
  EXPECT_EQ(parsed.chain.GetType(), Type::Counter);
  ASSERT_EQ(parsed.chain.nodes.size(), 2u);
  const auto* member = std::get_if<Member>(&parsed.chain.nodes.back().var);
  ASSERT_NE(member, nullptr);
  EXPECT_EQ(member->name, "counter");
  EXPECT_FALSE(member->implicit_call);
}

TEST_F(ElementParserTest,
       FrameActionCounterGetterContinuesIntoCounterCommands) {
  EXPECT_EQ(chain(79, 0, 1), "frame_action.counter.get()");
  EXPECT_EQ(chain(38, 2, -1, 0, 52, 0, 1),
            "stage.front.object[int:0].frame_action.counter.get()");

  ElementCode elm{53, -1, 8, 0, 0};
  elm.ForceBind({0, {v(17)}});
  auto parsed = chain(elm);
  EXPECT_EQ(parsed, "frame_action_ch[int:8].counter.set(int:17)");
  ASSERT_EQ(parsed.chain.nodes.size(), 5u);
  const auto* counter = std::get_if<Member>(&parsed.chain.nodes[2].var);
  ASSERT_NE(counter, nullptr);
  EXPECT_EQ(counter->name, "counter");
  EXPECT_EQ(parsed.chain.nodes[2].type, Type::Counter);
}

TEST_F(ElementParserTest, FrameActionCallbackCounterGetter) {
  curcall_args = {Type::FrameAction};
  const int flag = 0x7d << 24;

  auto parsed = chain(ElementCode{83, flag, 0, 1});
  EXPECT_EQ(parsed, "arg_0.counter.get()");
  EXPECT_EQ(parsed.chain.GetType(), Type::Int);
}

TEST_F(ElementParserTest, CurcallArgStr) {
  curcall_args = {Type::None, Type::String};

  int flag = 0x7d << 24;
  int idx = 1;
  ElementCode elm{83, (flag | idx), 2};
  auto parsed = chain(elm);
  EXPECT_EQ(parsed, "arg_1.left()");
  EXPECT_EQ(parsed.chain.GetType(), Type::String);
}

TEST_F(ElementParserTest, Movie) {
  {
    ElementCode elm{20, 2};
    elm.ForceBind({0, {v("mov1")}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mov.play_wait(str:mov1)");
    EXPECT_EQ(parsed.chain.GetType(), Type::None);
    EXPECT_EQ(parsed.CallFlags(), "[]as");
  }
  {
    ElementCode elm{20, 3};
    elm.ForceBind({1, {v("mov2"), v(0), v(0), v(420), v(420)}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mov.play_waitkey(str:mov2,int:0,int:0,int:420,int:420)");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
    EXPECT_EQ(parsed.CallFlags(), "[]as");
  }
}

TEST_F(ElementParserTest, ObjectMoviePreservesTaggedArguments) {
  Invoke invoke(0, {v("ef_dust01"), v(1)}, Type::None);
  invoke.named_arg = {{0, v(0)}};

  ElementCode elm{37, 2, -1, 114, 120};
  elm.ForceBind(std::move(invoke));

  EXPECT_EQ(chain(elm),
            "stage.back.object[int:114].create_movie[0]"
            "(str:ef_dust01,int:1,0=int:0)");
}

TEST_F(ElementParserTest, ObjectMovieWaitCallsAreAwaitable) {
  {
    ElementCode elm{37, 2, -1, 114, 122};
    elm.ForceBind({0, {v("ef_dust01")}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed,
              "stage.back.object[int:114].create_movie_wait[0]"
              "(str:ef_dust01)");
    EXPECT_EQ(parsed.CallFlags(), "[0]a");
    EXPECT_EQ(parsed.chain.GetType(), Type::None);
  }
  {
    ElementCode elm{37, 2, -1, 114, 143};
    elm.ForceBind({0, {v("ef_dust01")}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed,
              "stage.back.object[int:114].create_movie_waitkey[0]"
              "(str:ef_dust01)");
    EXPECT_EQ(parsed.CallFlags(), "[0]a");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
  }
  {
    auto parsed = chain(37, 2, -1, 114, 128);
    EXPECT_EQ(parsed, "stage.back.object[int:114].wait_movie()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
    EXPECT_EQ(parsed.chain.GetType(), Type::None);
  }
  {
    auto parsed = chain(37, 2, -1, 114, 142);
    EXPECT_EQ(parsed, "stage.back.object[int:114].wait_movie_key()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
  }
}

TEST_F(ElementParserTest, ObjectGanCallsAreSimpleObjectMethods) {
  {
    ElementCode elm{37, 2, -1, 106, 0x01000000};
    elm.ForceBind({0, {v("ef_noise02")}});

    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "stage.back.object[int:106].load_gan(str:ef_noise02)");
    EXPECT_EQ(parsed.chain.GetType(), Type::None);
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{37, 2, -1, 106, 0x01000001};
    elm.ForceBind({0, {v(0), v(1)}});

    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "stage.back.object[int:106].start_gan(int:0,int:1)");
    EXPECT_EQ(parsed.chain.GetType(), Type::None);
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
}

TEST_F(ElementParserTest, ObjectInitIsImplicitCall) {
  EXPECT_EQ(chain(37, 2, -1, 0, 35), "stage.back.object[int:0].init()");
}

TEST_F(ElementParserTest, MaskOperationsUseNativeCalls) {
  EXPECT_EQ(chain(135, -1, 1, 1), "mask[int:1].init()");

  {
    ElementCode elm{135, -1, 1, 4};
    elm.ForceBind({0, {}});
    EXPECT_EQ(chain(elm), "mask[int:1].x");
  }
  {
    ElementCode elm{135, -1, 1, 4};
    elm.ForceBind({1, {v(320)}});
    EXPECT_EQ(chain(elm), "mask[int:1].x = int:320");
  }
  {
    ElementCode elm{135, -1, 1, 2, 0};
    elm.ForceBind({0, {v(640), v(350), v(0), v(2)}});
    EXPECT_EQ(chain(elm), "mask[int:1].x_eve.set(int:640,int:350,int:0,int:2)");
  }
}

TEST_F(ElementParserTest, ObjectExistTypeIsImplicitGetterCall) {
  auto parsed = chain(38, 2, -1, 0, 174);
  EXPECT_EQ(parsed, "stage.front.object[int:0].exist_type()");
  EXPECT_EQ(parsed.chain.GetType(), Type::Int);
}

TEST_F(ElementParserTest, ObjectGetFilePathIsImplicitGetterCall) {
  auto parsed = chain(37, 2, -1, 10, 62);
  EXPECT_EQ(parsed, "stage.back.object[int:10].get_file_path()");
  EXPECT_EQ(parsed.chain.GetType(), Type::String);
  EXPECT_EQ(parsed.CallFlags(), "[]s");
}

TEST_F(ElementParserTest, ObjectButtonCallsAreSimpleCallables) {
  {
    ElementCode elm{37, 2, -1, 40, 42};
    elm.ForceBind({2, {v(0), v(1), v(2), v(3)}});

    auto parsed = chain(elm);
    EXPECT_EQ(parsed,
              "stage.back.object[int:40].set_button(int:0,int:1,int:2,int:3)");
    EXPECT_EQ(parsed.chain.GetType(), Type::None);
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }

  EXPECT_EQ(chain(37, 2, -1, 44, 61),
            "stage.back.object[int:44].clear_button()");
  EXPECT_EQ(chain(37, 2, -1, 44, 97),
            "stage.back.object[int:44].set_button_state_disable()");
  EXPECT_EQ(chain(37, 2, -1, 44, 118),
            "stage.back.object[int:44].get_button_state()");
  EXPECT_EQ(chain(37, 2, -1, 44, 123),
            "stage.back.object[int:44].get_button_hit_state()");
  EXPECT_EQ(chain(37, 2, -1, 44, 124),
            "stage.back.object[int:44].get_button_real_state()");
}

TEST_F(ElementParserTest, ObjectPropertyReadsAndSetterCommands) {
  {
    ElementCode elm{38, 2, -1, 0, 3};
    elm.ForceBind({0, {}});

    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "stage.front.object[int:0].x");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
    ASSERT_TRUE(std::holds_alternative<Member>(parsed.chain.nodes.back().var));
  }
  {
    ElementCode elm{38, 2, -1, 0, 3};
    elm.ForceBind({1, {v(42)}});

    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "stage.front.object[int:0].x = int:42");
    EXPECT_EQ(parsed.chain.GetType(), Type::None);
    const auto* setter = std::get_if<SetMember>(&parsed.chain.nodes.back().var);
    ASSERT_NE(setter, nullptr);
    EXPECT_EQ(setter->name, "x");
    EXPECT_EQ(setter->value, v(42));
  }
}

TEST_F(ElementParserTest, ObjectChildList) {
  EXPECT_EQ(chain(37, 2, -1, 0, 93, -1, 1, 35),
            "stage.back.object[int:0].child[int:1].init()");

  {
    ElementCode elm{37, 2, -1, 0, 93, 4};
    elm.ForceBind({0, {v(2)}});
    EXPECT_EQ(chain(elm), "stage.back.object[int:0].child.resize(int:2)");
  }
  {
    ElementCode elm{37, 2, -1, 0, 93, 3};
    elm.ForceBind({0, {}});
    EXPECT_EQ(chain(elm), "stage.back.object[int:0].child.size()");
  }
}

TEST_F(ElementParserTest, ObjectFList) {
  ElementCode elm{37, 2, -1, 40, 111, 2};
  elm.ForceBind({0, {v(1)}});
  EXPECT_EQ(chain(elm), "stage.back.object[int:40].F.resize(int:1)");
}

TEST_F(ElementParserTest, ObjectRepnoAlphaLists) {
  {
    ElementCode elm{38, 2, -1, 0, 141, 2};
    elm.ForceBind({0, {v(1)}});
    EXPECT_EQ(chain(elm), "stage.front.object[int:0].tr_rep.resize(int:1)");
  }
  {
    ElementCode elm{38, 2, -1, 0, 140, -1, 0, 0};
    elm.ForceBind({0, {v(128), v(1000), v(0), v(0)}});
    EXPECT_EQ(chain(elm),
              "stage.front.object[int:0].tr_rep_eve[int:0].set(int:128,int:"
              "1000,int:0,int:0)");
  }
}

TEST_F(ElementParserTest, ObjectRepnoEventTurn) {
  ElementCode elm{37, 2, -1, 109, 112, -1, 0, 2};
  elm.ForceBind({0, {v(0), v(20), v(1000), v(0), v(2)}});
  EXPECT_EQ(chain(elm),
            "stage.back.object[int:109].x_rep_eve[int:0].turn(int:0,int:20,int:"
            "1000,int:0,int:2)");
}

TEST_F(ElementParserTest, BgmTable) {
  {
    ElementCode elm{123, 2};
    elm.ForceBind({0, {v("song01"), v(1)}});
    EXPECT_EQ(chain(elm), "bgm_table.set_listen(str:song01,int:1)");
  }
}

TEST_F(ElementParserTest, Bgm) {
  {
    ElementCode elm{42, 0};
    elm.ForceBind({0, {v("song02"), v(1), v(2)}});
    EXPECT_EQ(chain(elm), "bgm.play(str:song02,int:1,int:2)");
  }
  {
    ElementCode elm{42, 17};
    elm.ForceBind({0, {v("song02")}});
    EXPECT_EQ(chain(elm), "bgm.ready_oneshot(str:song02)");
  }
  {
    ElementCode elm{42, 4};
    elm.ForceBind({1, {v(4000)}});
    EXPECT_EQ(chain(elm), "bgm.stop(int:4000)");
  }
}

TEST_F(ElementParserTest, BgmWaitCallsAreAwaitable) {
  {
    auto parsed = chain(42, 2);
    EXPECT_EQ(parsed, "bgm.play_wait()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
  }
  {
    auto parsed = chain(42, 3);
    EXPECT_EQ(parsed, "bgm.wait()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
    EXPECT_EQ(parsed.chain.GetType(), Type::None);
  }
  {
    auto parsed = chain(42, 14);
    EXPECT_EQ(parsed, "bgm.wait_key()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
  }
  {
    auto parsed = chain(42, 15);
    EXPECT_EQ(parsed, "bgm.wait_fade_key()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
  }
}

TEST_F(ElementParserTest, CounterOperationsAreCalls) {
  EXPECT_EQ(chain(40, -1, 1, 1), "counter[int:1].get()");
  EXPECT_EQ(chain(40, -1, 1, 2), "counter[int:1].reset()");
  EXPECT_EQ(chain(40, -1, 1, 9), "counter[int:1].start_real()");
  EXPECT_EQ(chain(40, -1, 1, 14), "counter[int:1].check_active()");

  {
    ElementCode elm{40, -1, 1, 0};
    elm.ForceBind({0, {v(42)}});
    EXPECT_EQ(chain(elm), "counter[int:1].set(int:42)");
  }
  {
    ElementCode elm{40, -1, 1, 10};
    elm.ForceBind({0, {v(10), v(20), v(100)}});
    EXPECT_EQ(chain(elm), "counter[int:1].start_frame(int:10,int:20,int:100)");
  }
}

TEST_F(ElementParserTest, CounterWaitCallsAreAwaitable) {
  {
    ElementCode elm{40, -1, 1, 6};
    elm.ForceBind({0, {v(100)}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "counter[int:1].wait(int:100)");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
    EXPECT_EQ(parsed.chain.GetType(), Type::None);
  }
  {
    ElementCode elm{40, -1, 1, 8};
    elm.ForceBind({0, {v(100)}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "counter[int:1].wait_key(int:100)");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
  }
}

TEST_F(ElementParserTest, SimpleCallableIgnoresOl) {
  ElementCode elm{42, 0};
  elm.ForceBind({99, {v("song02"), v(1), v(2)}});

  EXPECT_EQ(chain(elm), "bgm.play(str:song02,int:1,int:2)");
  EXPECT_TRUE(warnings.empty());
}

TEST_F(ElementParserTest, WipePreservesNamedArguments) {
  Invoke invoke(0, {}, Type::None);
  invoke.named_arg = {{8, v(1)}, {0, v(2)}, {3, list({v(10), v(11)})}};

  ElementCode elm{7};
  elm.ForceBind(std::move(invoke));

  EXPECT_EQ(chain(elm), "wipe.wipe[0](8=int:1,0=int:2,3=[int:10,int:11])");
}

TEST_F(ElementParserTest, WipeCommandMappings) {
  {
    ElementCode elm{51};
    elm.ForceBind({0, {v("mask")}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "wipe.wipe_mask[0](str:mask)");
    EXPECT_EQ(parsed.CallFlags(), "[0]a");
  }
  {
    ElementCode elm{50};
    elm.ForceBind({0, {v("mask")}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "wipe.wipe_mask_all[0](str:mask)");
    EXPECT_EQ(parsed.CallFlags(), "[0]a");
  }
  auto end = chain(33);
  EXPECT_EQ(end, "wipe.end()");
  EXPECT_EQ(end.CallFlags(), "[]");

  auto wait = chain(103);
  EXPECT_EQ(wait, "wipe.wait()");
  EXPECT_EQ(wait.chain.GetType(), Type::Int);
  EXPECT_EQ(wait.CallFlags(), "[]as");

  auto check = chain(109);
  EXPECT_EQ(check, "wipe.check()");
  EXPECT_EQ(check.chain.GetType(), Type::Int);
  EXPECT_EQ(check.CallFlags(), "[]s");
}

TEST_F(ElementParserTest, MwndObjectUsesGetterBeforeIndexing) {
  EXPECT_EQ(chain(38, 3, -1, 1, 30, -1, 2, 35),
            "stage.front.mwnd[int:1].object[int:2].init()");
  ElementCode setter{38, 3, -1, 1, 30, -1, 2, 93, -1, 0, 3};
  setter.ForceBind({1, {v(42)}});
  EXPECT_EQ(chain(setter),
            "stage.front.mwnd[int:1].object[int:2].child[int:0].x = int:42");
  ElementCode wait{38, 3, -1, 1, 54};
  auto parsed = chain(wait);
  EXPECT_EQ(parsed, "stage.front.mwnd[int:1].page()");
  EXPECT_EQ(parsed.CallFlags(), "[]as");
}

TEST_F(ElementParserTest, MwndButtonUsesObjectBindingAndGetterBeforeIndexing) {
  EXPECT_EQ(chain(38, 3, -1, 1, 32, -1, 2, 35),
            "stage.front.mwnd[int:1].button[int:2].init()");
  ElementCode setter{38, 3, -1, 1, 32, -1, 2, 93, -1, 0, 3};
  setter.ForceBind({1, {v(42)}});
  EXPECT_EQ(chain(setter),
            "stage.front.mwnd[int:1].button[int:2].child[int:0].x = int:42");
}

TEST_F(ElementParserTest, MwndFaceUsesObjectPropertiesAndDirectListGetter) {
  ElementCode resize{38, 3, -1, 0, 53, -1, 0, 54, 2};
  resize.ForceBind({1, {v(1)}});
  EXPECT_EQ(chain(resize),
            "stage.front.mwnd[int:0].face[int:0].x_rep.resize(int:1)");

  ElementCode setter{38, 3, -1, 0, 53, -1, 0, 145};
  setter.ForceBind({1, {v(0)}});
  EXPECT_EQ(chain(setter),
            "stage.front.mwnd[int:0].face[int:0].mask_no = int:0");

  ElementCode getter{38, 3, -1, 0, 53};
  getter.ForceBind({0, {}});
  EXPECT_EQ(chain(getter), "stage.front.mwnd[int:0].face");
}

TEST_F(ElementParserTest, GlobalFaceCommandsTargetActiveWindow) {
  ElementCode single{41};
  single.ForceBind({0, {v("portrait")}});
  EXPECT_EQ(chain(single), "mwnd.set_face(str:portrait)");

  ElementCode indexed{41};
  indexed.ForceBind({1, {v(1), v("portrait")}});
  EXPECT_EQ(chain(indexed), "mwnd.set_face(int:1,str:portrait)");

  EXPECT_EQ(chain(72), "mwnd.clear_face()");
}

TEST_F(ElementParserTest, Mwnd) {
  {
    ElementCode elm{22};
    elm.ForceBind({1, {v(1)}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.set_waku(int:1)");
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{22};
    elm.ForceBind({2, {v(1), v(-1)}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.set_waku(int:1,int:-1)");
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{9};
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.open()");
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{58};
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.open_wait()");
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{59};
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.open_nowait()");
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{10};
    EXPECT_EQ(chain(elm), "mwnd.close()");
  }
  {
    ElementCode elm{115};
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.page()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
  }
  {
    ElementCode elm{84};
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.msg_block()");
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{121};
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.msg_pp_block()");
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{11};
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.clear()");
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{12};
    elm.ForceBind({1, {v("hello")}});
    SetKidoku({105});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.print(str:hello)");
    EXPECT_EQ(parsed.chain.kidoku, 105);
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{12};
    elm.ForceBind({0, {v(123)}});
    SetKidoku({106});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.print(int:123)");
    EXPECT_EQ(parsed.chain.kidoku, 106);
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{21};
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.msg_wait()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
  }
  {
    ElementCode elm{13};
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.pp()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
  }
  {
    ElementCode elm{14};
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.r()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
  }
  {
    ElementCode elm{119};
    EXPECT_EQ(chain(elm), "mwnd.indent()");
  }
  {
    ElementCode elm{151};
    EXPECT_EQ(chain(elm), "mwnd.rep_pos_default()");
  }
  {
    ElementCode elm{151};
    elm.ForceBind({1, {v(320), v(240)}});
    EXPECT_EQ(chain(elm), "mwnd.rep_pos(int:320,int:240)");
  }
  {
    ElementCode elm{16};
    EXPECT_EQ(chain(elm), "mwnd.size_default()");
  }
  {
    ElementCode elm{16};
    elm.ForceBind({1, {v(15)}});
    EXPECT_EQ(chain(elm), "mwnd.size(int:15)");
  }
  {
    ElementCode elm{61};
    elm.ForceBind({1, {v("ruby")}});
    EXPECT_EQ(chain(elm), "mwnd.ruby_start(str:ruby)");
  }
  {
    ElementCode elm{61};
    elm.ForceBind({0, {}});
    EXPECT_EQ(chain(elm), "mwnd.ruby_end()");
  }
  {
    ElementCode elm{18};
    elm.ForceBind({0, {v(12345), v(7)}});
    SetKidoku({101});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.koe[0](int:12345,int:7)");
    EXPECT_EQ(parsed.chain.kidoku, 101);
    EXPECT_EQ(parsed.CallFlags(), "[0]");
  }
  {
    ElementCode elm{18};
    Invoke invoke;
    invoke.overload_id = 0;
    invoke.arg = {v(12345)};
    invoke.named_arg = {{0, v(1)}};
    elm.ForceBind(std::move(invoke));
    SetKidoku({102});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.koe[0](int:12345,0=int:1)");
    EXPECT_EQ(parsed.chain.kidoku, 102);
    EXPECT_EQ(parsed.CallFlags(), "[0]");
  }
  {
    ElementCode elm{90};
    elm.ForceBind({0, {v(12345)}});
    SetKidoku({103});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.koe_play_wait[0](int:12345)");
    EXPECT_EQ(parsed.chain.kidoku, 103);
    EXPECT_EQ(parsed.CallFlags(), "[0]a");
  }
  {
    ElementCode elm{91};
    elm.ForceBind({0, {v(12345), v(7)}});
    SetKidoku({104});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "mwnd.koe_play_wait_key[0](int:12345,int:7)");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
    EXPECT_EQ(parsed.chain.kidoku, 104);
    EXPECT_EQ(parsed.CallFlags(), "[0]a");
  }
}

TEST_F(ElementParserTest, GlobalKoe) {
  {
    ElementCode elm{87};
    elm.ForceBind({0, {v(12345), v(1)}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "exkoe(int:12345,int:1)");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
    EXPECT_EQ(parsed.CallFlags(), "[]s");
  }
  {
    ElementCode elm{88};
    elm.ForceBind({0, {v(12345)}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "exkoe_play_wait(int:12345)");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
    EXPECT_EQ(parsed.CallFlags(), "[]as");
  }
  {
    ElementCode elm{89};
    elm.ForceBind({0, {v(12345), v(1)}});
    auto parsed = chain(elm);
    EXPECT_EQ(parsed, "exkoe_play_wait_key(int:12345,int:1)");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
    EXPECT_EQ(parsed.CallFlags(), "[]as");
  }
  {
    ElementCode elm{68};
    EXPECT_EQ(chain(elm), "koe_stop()");
  }
  {
    ElementCode elm{68};
    elm.ForceBind({1, {v(500)}});
    EXPECT_EQ(chain(elm), "koe_stop(int:500)");
  }
}

TEST_F(ElementParserTest, Msgbk) {
  {
    ElementCode elm{145, 2};
    EXPECT_EQ(chain(elm), "msgbk.go_next_msg()");
  }
  {
    ElementCode elm{145, 3};
    elm.ForceBind({0, {v("line")}});
    EXPECT_EQ(chain(elm), "msgbk.add_msg(str:line)");
  }
  {
    ElementCode elm{145, 1};
    elm.ForceBind({0, {v("inserted")}});
    EXPECT_EQ(chain(elm), "msgbk.insert_msg(str:inserted)");
  }
  {
    ElementCode elm{145, 5};
    elm.ForceBind({0, {v("name")}});
    EXPECT_EQ(chain(elm), "msgbk.add_namae(str:name)");
  }
  {
    ElementCode elm{145, 4};
    elm.ForceBind({0, {v(12345)}});
    EXPECT_EQ(chain(elm), "msgbk.add_koe(int:12345)");
  }
  {
    ElementCode elm{145, 4};
    elm.ForceBind({0, {v(12345), v(7)}});
    EXPECT_EQ(chain(elm), "msgbk.add_koe(int:12345,int:7)");
  }
}

TEST_F(ElementParserTest, System) {
  {
    ElementCode elm{92, 13};
    EXPECT_EQ(chain(elm), "system.is_debug()");
  }
  {
    ElementCode elm{92, 7};
    elm.ForceBind({1, {v("msg")}});
    EXPECT_EQ(chain(elm), "system.debug_msgbox_ok(str:msg)");
  }
  {
    ElementCode elm{92, 6};
    elm.ForceBind({0, {v("file")}});
    EXPECT_EQ(chain(elm), "system.check_file_exist(str:file)");
  }
  {
    ElementCode elm{92, 2};
    elm.ForceBind({0, {v("dummy"), v(123), v("key")}});
    EXPECT_EQ(chain(elm), "system.check_dummy(str:dummy,int:123,str:key)");
  }
  {
    ElementCode elm{63, 11};
    EXPECT_EQ(chain(elm), "syscom.btn_enable_all()");
  }
  {
    ElementCode elm{63, 11};
    elm.ForceBind({1, {v(3)}});
    EXPECT_EQ(chain(elm), "syscom.btn_enable(int:3)");
  }
  {
    fail_on_warn = false;

    ElementCode elm{63, 11};
    elm.ForceBind({99, {}});
    EXPECT_EQ(chain(elm), "syscom.btn_enable_all()");
    EXPECT_THAT(warnings,
                ElementsAre(HasSubstr("[Callable] overload 99 not found")));
  }
}

TEST_F(ElementParserTest, GlobalPcmCommandsAreImmediateCalls) {
  ElementCode play{43, 0};
  play.ForceBind({0, {v("se_sasareru")}});
  auto parsed = chain(play);
  EXPECT_EQ(parsed, "pcm.play(str:se_sasareru)");
  EXPECT_EQ(parsed.chain.GetType(), Type::None);
  EXPECT_EQ(parsed.CallFlags(), "[]s");

  auto stop = chain(43, 1);
  EXPECT_EQ(stop, "pcm.stop()");
  EXPECT_EQ(stop.chain.GetType(), Type::None);
  EXPECT_EQ(stop.CallFlags(), "[]s");

  ElementCode explicit_stop{43, 1};
  explicit_stop.ForceBind({0, {}});
  EXPECT_EQ(chain(explicit_stop), "pcm.stop()");
}

TEST_F(ElementParserTest, SeCommandsPreserveArgumentsAndWaitFlags) {
  const std::pair<int, const char*> plays[] = {
      {0, "play"}, {6, "play_by_koe_no"}, {9, "play_by_se_no"}};
  for (const auto& [id, name] : plays) {
    ElementCode call{45, id};
    call.ForceBind({0, {v(3)}});
    EXPECT_EQ(chain(call), std::string("se.") + name + "(int:3)");
  }
  ElementCode file{45, 5};
  file.ForceBind({0, {v("effect")}});
  EXPECT_EQ(chain(file), "se.play_by_file_name(str:effect)");
  ElementCode stop{45, 7};
  stop.ForceBind({1, {v(1000)}});
  EXPECT_EQ(chain(stop), "se.stop(int:1000)");
  EXPECT_EQ(chain(45, 7), "se.stop()");
  ElementCode volume{45, 1};
  volume.ForceBind({1, {v(128), v(500)}});
  EXPECT_EQ(chain(volume), "se.set_volume(int:128,int:500)");
  EXPECT_EQ(chain(45, 2), "se.set_volume_max()");
  EXPECT_EQ(chain(45, 3), "se.set_volume_min()");
  EXPECT_EQ(chain(45, 4).chain.GetType(), Type::Int);
  EXPECT_EQ(chain(45, 11).chain.GetType(), Type::Int);
  EXPECT_EQ(chain(45, 8).CallFlags(), "[]as");
  EXPECT_EQ(chain(45, 10).CallFlags(), "[]as");
  EXPECT_EQ(chain(45, 8).chain.GetType(), Type::None);
  EXPECT_EQ(chain(45, 10).chain.GetType(), Type::Int);
}

TEST_F(ElementParserTest, Pcmch) {
  {
    ElementCode elm{44, -1, 0, 0};
    elm.ForceBind({0, {}});
    EXPECT_EQ(chain(elm), "pcmch_list[int:0].play()");
  }
}

TEST_F(ElementParserTest, PcmchWaitCallsAreAwaitable) {
  {
    auto parsed = chain(44, -1, 0, 1);
    EXPECT_EQ(parsed, "pcmch_list[int:0].play_wait()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
  }
  {
    auto parsed = chain(44, -1, 0, 3);
    EXPECT_EQ(parsed, "pcmch_list[int:0].wait()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
    EXPECT_EQ(parsed.chain.GetType(), Type::None);
  }
  {
    auto parsed = chain(44, -1, 0, 6);
    EXPECT_EQ(parsed, "pcmch_list[int:0].wait_key()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
  }
  {
    auto parsed = chain(44, -1, 0, 7);
    EXPECT_EQ(parsed, "pcmch_list[int:0].wait_fade_key()");
    EXPECT_EQ(parsed.CallFlags(), "[]as");
    EXPECT_EQ(parsed.chain.GetType(), Type::Int);
  }
}

TEST_F(ElementParserTest, UsrcmdGlobal) {
  global_commands = {Command{.scene_id = 1, .offset = 2, .name = "$$cmd"}};
  ResetParser();

  ElementCode elm{2113929216};
  elm.ForceBind({0, {v(20)}});
  EXPECT_EQ(chain(elm), "@1.2:$$cmd(int:20)");
}

TEST_F(ElementParserTest, LayeredMwndCommandsPreserveStageSelection) {
  EXPECT_EQ(chain(37, 3, -1, 0, 10), "stage.back.mwnd[int:0].layer");
  ElementCode setter{73, 3, -1, 1, 10};
  setter.ForceBind({1, {v(-2)}});
  EXPECT_EQ(chain(setter), "stage.next.mwnd[int:1].layer = int:-2");
  EXPECT_EQ(chain(38, 3, 3), "stage.front.mwnd.close_nowait()");
  ElementCode create{37, 1};
  create.ForceBind({0, {v(3)}});
  EXPECT_EQ(chain(create), "stage.back.create_mwnd(int:3)");
}

}  // namespace siglus_test
