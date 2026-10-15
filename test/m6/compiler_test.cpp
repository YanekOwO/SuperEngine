// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
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
//
// -----------------------------------------------------------------------

#include <gtest/gtest.h>

#include "m6/compiler_pipeline.hpp"
#include "m6/source_buffer.hpp"
#include "m6/vm_factory.hpp"
#include "srbind/module.hpp"
#include "utilities/string_utilities.hpp"
#include "vm/disassembler.hpp"
#include "vm/value.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace m6test {

using namespace m6;
using namespace serilang;
namespace fs = std::filesystem;

class CompilerTest : public ::testing::Test {
 public:
  struct error {
    std::string_view diagnose;
  };

  // Holds everything we care about after one script run
  struct ExecutionResult {
    serilang::Value last;  ///< value left on the VM stack (result)
    std::string output;    ///< text produced on std::cout
    std::string errors;    ///< text produced on std::cerr
    std::string disasm;    ///< human readable disassembly

    bool operator==(std::string_view rhs) const {
      return errors.empty() && trim_cp(output) == trim_sv(rhs);
    }
    bool operator==(error rhs) const {
      return output.empty() && trim_cp(errors) == trim_sv(rhs.diagnose);
    }

    friend std::ostream& operator<<(std::ostream& os,
                                    const ExecutionResult& res) {
      if (!res.errors.empty())
        os << "\nErrors:\n" << res.errors;
      os << "\nOutput:\n" << res.output;
      os << "\nDisassembly:\n" << res.disasm;
      return os;
    }
  };

  std::stringstream inBuf;
  std::shared_ptr<GarbageCollector> gc = std::make_shared<GarbageCollector>();

  // Compile + run `source`.
  [[nodiscard]] ExecutionResult Run(std::string source) {
    return RunWith(std::move(source), nullptr);
  }

  [[nodiscard]] ExecutionResult RunWith(
      std::string source,
      std::function<void(serilang::VM&)> init_vm) {
    std::stringstream outBuf, errBuf;
    ExecutionResult r;

    // ── compile ────────────────────────────────────────────────────
    auto vm = m6::VMFactory::Create(gc, outBuf, inBuf, errBuf);
    if (init_vm)
      init_vm(vm);
    m6::CompilerPipeline pipe(gc, false);
    auto sb = SourceBuffer::Create(std::move(source), "<CompilerTest>");
    pipe.compile(sb);

    // any compile-time diagnostics?
    if (!pipe.Ok()) {
      r.errors += pipe.FormatErrors();
      return r;
    }

    auto chunk = pipe.Get();
    if (!chunk) {
      r.errors += "internal: pipeline returned null chunk\n";
      return r;
    }
    r.disasm = Disassembler().Dump(*chunk);

    // ── run ────────────────────────────────────────────────────────
    try {
      r.last = vm.Evaluate(chunk);
    } catch (std::exception const& ex) {
      errBuf << ex.what();
    }

    r.output += outBuf.str();
    r.errors += errBuf.str();
    return r;
  }
  [[nodiscard]] ExecutionResult Interpret(
      std::initializer_list<std::string> src) {
    std::stringstream outBuf, errBuf;
    ExecutionResult r;
    auto vm = m6::VMFactory::Create(gc, outBuf, inBuf, errBuf);

    for (const auto& line : src) {
      m6::CompilerPipeline pipe(gc, /*repl=*/true);
      auto sb = SourceBuffer::Create(line, "<Compiler Test>");
      pipe.compile(sb);

      if (!pipe.Ok()) {
        r.errors += pipe.FormatErrors();
        continue;
      }

      auto chunk = pipe.Get();
      if (!chunk) {
        r.errors +=
            "internal: pipeline returned null chunk, when compiling " + line;
        continue;
      }

      r.disasm += Disassembler().Dump(*chunk);

      EXPECT_NO_THROW(r.last = vm.Evaluate(chunk));
    }

    r.output += outBuf.str();
    r.errors += errBuf.str();
    return r;
  }
};

TEST_F(CompilerTest, ConstantArithmetic) {
  {
    auto res = Run(R"( print([1+2, 2**3, 1-3], end=""); )");
    EXPECT_EQ(res, "[3,8,-2]");
  }
  {
    auto res = Run(R"(
print(true&&false,
false||true,
true && true||false,
true&&(true&&false));
 )");
    EXPECT_EQ(res, "false true true false\n");
  }
  {  // Negative division/modulo rules
    auto res = Run(
        R"( print(((-7 / 3) * 10) + (-7 % 3) + ((7 / -3) * 10) + (7 % -3)); )");
    EXPECT_EQ(res, "-40\n");
  }
  {  // Power associativity
    auto res = Run(R"( print((2 ** 3 ** 2) + (7) + (9)); )");
    EXPECT_EQ(res, "528\n");
  }
  {  // Bitwise edge cases with negatives
    auto res = Run(
        R"( print((~0)+((-1)>>1)+((-2)>>1)+((-3)&0xFF)+(0x1234^0xFFFF)); )");
    EXPECT_EQ(res, "61125\n");
  }
}

TEST_F(CompilerTest, Comments) {
  {
    auto res = Run("// leading comment\nprint(1); /* trailing */");
    EXPECT_EQ(res, "1\n");
  }

  {
    auto res = Run("/* unterminated");
    EXPECT_EQ(res, error{R"(
At file '<CompilerTest>' Unterminated block comment.
1│ /* unterminated
   ^^^^^^^^^^^^^^^
)"});
  }

  {
    auto res = Run("foo /*comment*/");
    EXPECT_EQ(res, error{R"(
At file '<CompilerTest>' Expected ';'.
1│ foo /*comment*/
      ^
)"});
  }
}

TEST_F(CompilerTest, GlobalVariable) {
  auto res = Run("a = 10;\n print(a + 5);");
  EXPECT_EQ(res, "15\n");
}

TEST_F(CompilerTest, FunctionReadsUpdatedGlobalNamespace) {
  auto res = Run(R"(
a = 12;
fn foo(){ return a; }
a = 34;
print(foo());
)");

  EXPECT_EQ(res, "34\n");
}

TEST_F(CompilerTest, MultipleStatements) {
  auto res = Run("x = 4;\n y = 6;\n print(x * y);");
  EXPECT_EQ(res, "24\n");
}

TEST_F(CompilerTest, StringMethods) {
  auto res = Run(R"(
s = "abcdef";
print(s.len());
print(s.substr(1, 3));
print(s.substr(2));
print(s.substr(-2));
print(s.prefix(4));
print(s.prefix(10));
print(s.suffix(2));
print(s.suffix(10));
print(s[0], s[-1], sep=",", end="");
)");

  EXPECT_EQ(res, "6\nbcd\ncdef\nef\nabcd\nabcdef\nef\nabcdef\na,f");
}

TEST_F(CompilerTest, StringMethodErrors) {
  auto res =
      Interpret({R"( s = "abc"; )", R"( s.substr(10); )", R"( s.prefix(-1); )",
                 R"( s.suffix("bad"); )", R"( s[3]; )", R"( "ok"; )"});

  EXPECT_EQ(res, R"(
string index '10' out of range
prefix length must be non-negative
suffix length must be integer, but got: <str: bad>
string index '3' out of range
ok
)");
}

TEST_F(CompilerTest, List) {
  {
    auto res = Run("x=1; a=[x,x+1,x*3]; print(a);");
    EXPECT_EQ(res, "[1,2,3]\n");
  }
  {
    auto res = Run("a=[1,2]; a[0]=2; a[1]=3; print(a);");
    EXPECT_EQ(res, "[2,3]\n");
  }
}

TEST_F(CompilerTest, Dict) {
  {
    auto res = Run(R"( x=1; a={"1":x,"2":x+1,"3":1+x*2}; print(a); )");
    EXPECT_EQ(res, "{1:1,2:2,3:3}\n");
  }
  {
    auto res = Run(R"( a={"1":1}; a["1"]+=1; print(a); )");
    EXPECT_EQ(res, "{1:2}\n");
  }
}

TEST_F(CompilerTest, If) {
  auto res = Run(R"(
result = "none";
one = 1;
two = 2;
if(one < two){
  if(one+1 < two) result = "first";
  else { result = one + two; }
} else result = "els";
print(result);
)");

  EXPECT_EQ(res, "3\n");
}

TEST_F(CompilerTest, VariableScope) {
  auto res = Run(R"(
fn foo(){
  a = 1;
  b = a + a;
  global a;
  result = a + b;
  a = b;
  return result;
}
a = 12;
print(foo(), a);
)");

  EXPECT_EQ(res, "14 2\n");
}

TEST_F(CompilerTest, FunctionFastLocalsAboveByteRange) {
  constexpr int kLocalCount = 300;

  std::string source = "fn foo(){\n";
  for (int i = 0; i <= kLocalCount; ++i)
    source += std::format("  v{} = {};\n", i, i);
  source += std::format("  return v{};\n", kLocalCount);
  source += "}\nprint(foo(), end=\"\");\n";

  auto res = Run(source);
  EXPECT_EQ(res, "300");
}

TEST_F(CompilerTest, While) {
  auto res = Run(R"(
sum = 0;
i = 1;
while(i <= 10){ sum+=i; i+=1; }
print(sum);
)");

  EXPECT_EQ(res, "55\n");
}

TEST_F(CompilerTest, For) {
  auto res = Run(R"(
fact = 1;
for(i=1; i<10; i+=1) fact *= i;
print(fact);
)");

  EXPECT_EQ(res, "362880\n");
}

TEST_F(CompilerTest, Function) {
  {
    auto res = Run(R"(
fn print_twice(msg) {
  print(msg);
  print(msg);
}
print_twice("hello");
)");

    EXPECT_EQ(res, "hello\nhello\n");
  }

  {
    auto res = Run(R"(
fn print_time(){ t = time(); print(t); }
print_time();
)");

    auto isNumber = [](std::string s) {
      if (!s.empty() && s.back() == '\n')
        s.pop_back();
      static const std::regex re{R"(^[+-]?\d+(\.\d+)?$)"};
      return std::regex_match(s, re);
    };
    EXPECT_TRUE(isNumber(res.output)) << res;
  }

  {
    auto res = Run(R"(
fn return_plus_1(x) { return x+1; }
print(return_plus_1(123));
)");

    EXPECT_EQ(res, "124\n");
  }

  {
    auto res = Run(R"(
fn complex_func(a, b, c=3, *args, d=10, e=5, **kwargs){
  print(a,b,c,d,e,args,kwargs);
}
complex_func(1, 2, 10, 20, 30, 40, 50, extra="foo");
complex_func(10 ,20);
)");

    EXPECT_EQ(res, "1 2 10 20 30 [40,50] {extra:foo}\n10 20 3 10 5 [] {}\n");
  }

  {
    auto res = Run(R"(
fn foo(a=1, b=2, **kwargs){
  print("a =", a);
  print("b =", b);
  print("extra =", kwargs);
}
foo(b=10, a=20);
)");

    EXPECT_EQ(res, "a = 20\nb = 10\nextra = {}\n");
  }

  {
    auto res = Run(R"(
try{ a = 1; a(); }
catch(e){ print(e); }
)");

    EXPECT_EQ(res, "'<int: 1>' object is not callable.");
  }
}

TEST_F(CompilerTest, RecursiveFunction) {
  {
    auto res = Run(R"(
fn fib(n) {
  if (n < 2) return 1;
  return fib(n-1) + fib(n-2);
}
print(fib(10));
)");

    EXPECT_EQ(res, "89\n");
  }
}

TEST_F(CompilerTest, Class) {
  {
    auto res = Run(R"(
class Klass{
  fn foo(){ return 1; }
  fn boo(x,y){ return x+y; }
}

print(Klass);
klass = Klass();
print(klass, klass.foo(), klass.boo(2,3), end="", sep=",");
)");

    EXPECT_EQ(res, "<class Klass>\n<Klass object>,1,5");
  }

  {
    auto res = Run(R"(
class Klass{
  fn foo(self, x){
    self.result += "*" * x + "0";
    if(x > 1) foo(self, x-1);
  }
}

inst = Klass();
inst.result = "";

foo = inst.foo;
foo(5);

print(inst.result);
)");

    EXPECT_EQ(res, "*****0****0***0**0*0\n");
  }

  {
    auto res = Run(R"(
class Point2{
  fn __init__(self, x=0, y=0){ self.x=x; self.y=y; }
  fn dist_sq(self){ return self.x*self.x + self.y*self.y; }
}

pt1 = Point2(4,5);
pt2 = Point2(10);
pt3 = Point2();

print(pt1.dist_sq(), pt2.dist_sq(), pt3.dist_sq());
)");

    EXPECT_EQ(res, "41 100 0\n");
  }

  {
    auto res = Run(R"(
class A{}
class B{}
try{ A() + B(); }
catch(e){ print(e); }
)");
    EXPECT_EQ(res,
              "no match for 'operator +' (operand type <A object>,<B object>)");
  }

  {
    auto res = Run(R"(
class A{}
try{ a = A(); b = a.missing; print(b); }
catch(e){ print(e); }
)");
    EXPECT_EQ(res, "'<A object>' object has no member 'missing'");
  }
}

TEST_F(CompilerTest, PropertiesReadWriteAndInstanceIsolation) {
  auto res = Run(R"(
class Box {
  fn __init__(self, initial) {
    self.reads = 0;
    self.writes = 0;
    self.size = initial;
  }
  property size {
    get { self.reads += 1; return self._size; }
    set { self.writes += 1; self._size = value; return 123; }
  }
  property doubled { get { return self.size * 2; } }
  fn peek(self) { return self.size; }
  fn static_number() { return 9; }
}
a = Box(2);
b = Box(9);
a.size = 5;
print(a.size, b.size, a.doubled, a.peek());
print(a.reads, a.writes, b.reads, b.writes, a.static_number());
for(i = 0; i < 100; i += 1) a.size = i;
print(a.size, a.writes);
)");
  EXPECT_EQ(res, "5 9 10 5\n3 2 1 1 9\n99 102\n");
}

TEST_F(CompilerTest, PropertiesMissingAccessors) {
  auto res = Run(R"(
class Box {
  property read_only { get { return 7; } }
  property write_only { set { self.stored = value; } }
}
b = Box();
b.write_only = 4;
print(b.read_only, b.stored);
try { b.read_only = 8; } catch(e) { print(e); }
try { print(b.write_only); } catch(e) { print(e); }
try { b.read_only += 1; } catch(e) { print(e); }
try { b.write_only += 1; } catch(e) { print(e); }
print(b.read_only, b.stored);
)");
  EXPECT_EQ(res, R"(
7 4
property 'read_only' has no setter
property 'write_only' has no getter
property 'read_only' has no setter
property 'write_only' has no getter
7 4
)");
}

TEST_F(CompilerTest, PropertyCompoundAssignmentEvaluationOrder) {
  auto res = Run(R"(
trace = "";
class Box {
  property size {
    get { global trace; trace += "G"; return self.stored; }
    set { global trace; trace += "S"; self.stored = value; }
  }
}
b = Box();
b.stored = 3;
fn receiver() { global trace; trace += "R"; return b; }
fn rhs() { global trace; trace += "V"; return 4; }
receiver().size += rhs();
print(trace, b.stored);
)");
  EXPECT_EQ(res, "RGVS 7\n");
}

TEST_F(CompilerTest, PropertyGlobalsAndContextualIdentifiers) {
  auto res = Run(R"(
global self, value;
self = 100;
value = 200;
size = 7;
property = 10;
get = 11;
set = 12;
class Box {
  property size {
    get { return self.stored + size; }
    set { self.stored = value; }
  }
  fn get(self) { return self.stored; }
  fn set(self, v) { self.stored = v; }
  fn property() { return 13; }
}
b = Box();
b.size = 4;
print(b.size, b.stored, self, value, property, get, set, b.property());
)");
  EXPECT_EQ(res, "11 4 100 200 10 11 12 13\n");
}

TEST_F(CompilerTest, PropertyReturnsCallableAndFallsThrough) {
  auto res = Run(R"(
fn twice(x) { return x * 2; }
class Box {
  property callback { get { return twice; } }
  property empty { get {} }
  property value { get { return self.stored; } set { self.stored = value; } }
}
b = Box();
b.value = 5;
print(b.callback(4), b.empty, b.value);
)");
  EXPECT_EQ(res, "8 nil 5\n");
}

TEST_F(CompilerTest, PropertyExceptions) {
  auto res = Run(R"(
class Box {
  property value {
    get { throw "getter failed"; }
    set { if(value < 0) throw "setter failed"; self.stored = value; }
  }
}
b = Box();
try { print(1 + b.value); } catch(e) { print(e); }
try { b.value = -1; print("unreachable"); } catch(e) { print(e); }
b.value = 6;
print(b.stored, 10 + 2);
)");
  EXPECT_EQ(res, "getter failed\nsetter failed\n6 12\n");
}

TEST_F(CompilerTest, PropertyAccessorsCanAwait) {
  auto res = Run(R"(
class Box {
  property value {
    get { return await async.Sleep(1, self.stored); }
    set { self.stored = await async.Sleep(1, value); return 123; }
  }
  property doubled { get { return self.value * 2; } }
}
b = Box();
b.value = 3;
b.value += 2;
print(1 + b.value, b.doubled, b.stored);
)");
  EXPECT_EQ(res, "6 10 5\n");
}

TEST_F(CompilerTest, PropertyAccessorsCanYield) {
  auto res = Run(R"(
class Box {
  property value {
    get { yield "get"; return self.stored; }
    set { yield "set"; self.stored = value; return 123; }
  }
}
b = Box();
fn write() { b.value = 6; return b.stored; }
writer = spawn write();
print(await writer);
print(await writer);
fn read() { return 1 + b.value; }
reader = spawn read();
print(await reader);
print(await reader);
)");
  EXPECT_EQ(res, "set\n6\nget\n7\n");
}

TEST_F(CompilerTest, ClassOperators) {
  auto res = Run(R"(
class A{
  fn __init__(self){ self.x = 1; self.y = 2; }
  fn __getitem__(self, key){
    if(key == "x") return self.x;
    else if(key == "y") return self.y;
    else return nil;
  }
  fn __setitem__(self, key, val){
    if(key == "x") self.x = val;
    else if(key == "y") self.y = val;
    else return nil;
  }
}

a = A();
a["x"] = 5; a["y"] = 6;
print("x = ", a["x"], ", y = ", a["y"], sep="", end="");
)");

  EXPECT_EQ(res, "x = 5, y = 6");
}

TEST_F(CompilerTest, InstanceMagicMethods) {
  auto res = Run(R"(
class Box{
  fn __init__(self, value){ self.value = value; }
  fn __add__(self, other){ return Box(self.value + other.value); }
  fn __radd__(self, other){ return Box(other + self.value); }
  fn __neg__(self){ return Box(-self.value); }
}

a = Box(2);
b = Box(5);
print((a + b).value, (-a).value, (3 + b).value);
)");

  EXPECT_EQ(res, "7 -2 8\n");
}

TEST_F(CompilerTest, NativeInstanceMagicMethods) {
  struct NativeBox {
    explicit NativeBox(int v = 0) : value(v) {}

    int Get() const { return value; }
    void Set(int v) { value = v; }
    int Add(const NativeBox* other) const {
      return value + (other ? other->value : 0);
    }
    int RAdd(int other) const { return other + value; }
    int Neg() const { return -value; }
    int GetItem(std::string key) const {
      auto it = items.find(key);
      if (it == items.end())
        return 0;
      return it->second;
    }
    void SetItem(std::string key, int val) { items[std::move(key)] = val; }

    int value = 0;
    std::unordered_map<std::string, int> items;
  };

  auto res = RunWith(
      R"(
box = NativeBox(10);
other = NativeBox(5);
box["score"] = 7;
box["bonus"] = 9;
print(box + other, 3 + box, -box, box["score"], box["bonus"]);
)",
      [&](serilang::VM& vm) {
        using srbind::arg;
        using srbind::class_;
        using srbind::init;

        srbind::module_ mod(vm.gc_.get(), vm.builtins_.get());
        class_<NativeBox> box(mod, "NativeBox");
        box.def(init<int>(), arg("value") = 0)
            .def("__add__", &NativeBox::Add, arg("other"))
            .def("__radd__", &NativeBox::RAdd, arg("other"))
            .def("__neg__", &NativeBox::Neg)
            .def("__getitem__", &NativeBox::GetItem, arg("key"))
            .def("__setitem__", &NativeBox::SetItem, arg("key"), arg("value"));
      });

  EXPECT_EQ(res, "15 13 -10 7 9\n");
}

TEST_F(CompilerTest, Coroutine) {
  {
    auto res = Run(R"(
fn foo(){for(i=0;;i+=1) yield i;}
f = spawn foo();
for(i=0;i<5;i+=1)
  print(await f);
)");

    EXPECT_EQ(res, "0\n1\n2\n3\n4\n");
  }

  {
    auto res = Run(R"(
fn deep(n){
  if(n<=0) return 0;
  return n + await spawn deep(n-1);
}

print(await spawn deep(1000));
)");

    EXPECT_EQ(res, "500500\n");
  }

  {
    auto res = Run(R"(
fn wait_for(n){
  for(i=0;i<n;i+=1) yield;
  return n;
}

for(i=1;i<=5;i+=1){
  result = await spawn wait_for(i);
  print(result);
}
)");

    // FIXME: this should be "1\n2\n3\n4\n5\n"
    EXPECT_EQ(res, "nil\nnil\nnil\nnil\nnil\n");
  }

  {
    auto res = Run(R"(
fn foo(){
  for(i=0;i<3;i+=1) yield i;
  return 42;
}
f = spawn foo();
tmp = await f; tmp = await f; tmp = await f;
print(await f);
)");
    EXPECT_EQ(res, "42\n");
  }

  {
    auto res = Run(R"(
fn bad(){ throw "boom"; }
f = spawn bad();
try{ await f; print("nope"); }
catch(e){ print(e); }
)");
    EXPECT_EQ(res, "boom\n");
  }

  {
    auto res = Run(R"(
fn foo(){ yield; return 99; }
f = spawn foo();
x = await f;
print(x, await f, await f);
)");
    EXPECT_EQ(res, "nil 99 99\n");
  }
}

TEST_F(CompilerTest, Async_Timeout) {
  {  // rejected
    auto res = Run(R"(
fn foo(){ return await async.Sleep(30, result = 123); }
future = spawn foo();
try{
  result = await async.Timeout(future, 10);
  print(result);
} catch(e) { print(e); }
)");
    EXPECT_EQ(res, "Timeout after 10 ms");
  }
  {  // resolve
    auto res = Run(R"(
fn foo(){ return await async.Sleep(5, result = 123); }
try{
  result = await async.Timeout(spawn foo(), 20);
  print(result);
} catch(e) { print(e); }
)");
    EXPECT_EQ(res, "123");
  }
  {  // timer should start at await command, not future creation
    auto res = Run(R"(
try{
  future = async.Timeout(async.Sleep(5, result = 123), 20);
  await async.Sleep(20);
  print(await future);
} catch(e) { print(e); }
)");
    EXPECT_EQ(res, "123");
  }
}

TEST_F(CompilerTest, Async_Gather) {
  {
    auto res = Run(R"(
fn foo(n){ return await async.Sleep(n, n+1); }
fn boo(n){ return await async.Sleep(n, n**3); }
try{
  result = await async.Gather(spawn foo(1), spawn boo(2));
  print(result);
} catch(e) { print(e); }
)");
    EXPECT_EQ(res, "[2,8]");
  }

  {
    auto res = Run(R"(
try{
  result = await async.Gather();
  print(result);
} catch(e) { print(e); }
)");
    EXPECT_EQ(res, "[]");
  }

  {
    auto res = Run(R"(
fn foo(n){ throw "foo_error";  }
fn boo(n){ return await async.Sleep(n, n**3); }
future = async.Gather(spawn foo(1), spawn boo(2));
try{
  print(await future);
} catch(e) { print(e); }
try{
  print(await future);
} catch(e) { print(e); }
)");
    EXPECT_EQ(res, "foo_error\nfoo_error");
  }
}

TEST_F(CompilerTest, Async_Race) {
  {
    auto res = Run(R"(
fn foo(n){ return await async.Sleep(10, n+1); }
fn boo(n){ return await async.Sleep(30, n**3); }
try{
  result = await async.Race(spawn foo(1), spawn boo(2));
  print(result);
} catch(e) { print(e); }
)");
    EXPECT_EQ(res, "2");
  }

  {
    auto res = Run(R"(
try{
  result = await async.Race();
  print(result);
} catch(e) { print(e); }
)");
    EXPECT_EQ(res, "nil");
  }
}

TEST_F(CompilerTest, Import) {
  struct Source {
    fs::path path;
    std::string modname;
    Source(fs::path p, std::string src) : path(p), modname(p.string()) {
      if (path.extension() != ".sr")
        path.replace_filename(path.string() + ".sr");

      std::ofstream ofs(path.string());
      ofs << std::move(src);
    }
    ~Source() { fs::remove(path); }
  };

  // basic imports
  {
    Source srcx("modulex", R"(
val = 123;
fn func(){ return val; }
)");

    auto res = Run(std::format(R"(
import {0};
from {0} import val as v;

val = 999;

print({0}.val);
print({0}.func());
print(v);
)",
                               srcx.modname));

    EXPECT_EQ(res, "123\n123\n123\n");
  }

  {
    Source srcx("module_live", R"(
val = 123;
fn func(){ return val; }
)");

    auto res = Run(std::format(R"(
import {0} as m;
m.val = 456;
print(m.func());
)",
                               srcx.modname));

    EXPECT_EQ(res, "456\n");
  }

  // name collisions
  {
    Source srcx("modulex", R"(
val = 123;
)");
    Source srcy("moduley", R"(
modulex = "456";
)");

    auto res = Run(std::format(R"(
import {0};
from {1} import {0};

print({0});
)",
                               srcx.modname, srcy.modname));

    EXPECT_EQ(res, "456\n");
  }

  // circular import
  {
    Source srca("circ_a", R"(
import circ_b;
fn func_a(){ return "A"; }
fn call_b(){ return circ_b.func_b(); }
)");
    Source srcb("circ_b", R"(
import circ_a;
fn func_b(){ return "B"; }
fn call_a(){ return circ_a.func_a(); }
)");

    auto res = Run(R"(
import circ_a as a;
import circ_b as b;
print(a.call_b());
print(b.call_a());
)");

    EXPECT_EQ(res, "B\nA\n");
  }

  {  // import missing name
    Source mod("repl_mod_missing_attr", R"(val = 1;)");
    auto res = Run(std::format(R"(
try{{ from {} import missing;}}
catch(e){{ print(e); }}
)",
                               mod.modname));
    EXPECT_EQ(res, "module 'repl_mod_missing_attr' has no attribute 'missing'");
  }

  {  // module missing attribute
    Source mod("repl_mod_no_attr", R"(val = 1;)");
    auto res = Run(std::format(R"(
import {} as m;
try{{ a = m.missing; }}
catch(e){{ print(e); }}
)",
                               mod.modname));
    EXPECT_EQ(res, "module 'repl_mod_no_attr' has no attribute 'missing'");
  }
}

TEST_F(CompilerTest, ImportUsesMovedVmInstance) {
  struct Source {
    fs::path path;
    std::string modname;
    Source(fs::path p, std::string src) : path(p), modname(p.string()) {
      if (path.extension() != ".sr")
        path.replace_filename(path.string() + ".sr");

      std::ofstream ofs(path.string());
      ofs << std::move(src);
    }
    ~Source() { fs::remove(path); }
  };

  Source srcx("module_after_vm_move", R"(
val = ambient_value;
)");

  std::stringstream outBuf, errBuf;
  auto moved_vm = std::make_unique<serilang::VM>(
      m6::VMFactory::Create(gc, outBuf, inBuf, errBuf));

  m6::CompilerPipeline pipe(gc, false);
  auto sb = SourceBuffer::Create(std::format(R"(
ambient_value = 321;
import {} as m;
print(m.val);
)",
                                             srcx.modname),
                                 "<CompilerTest>");
  pipe.compile(sb);
  ASSERT_TRUE(pipe.Ok()) << pipe.FormatErrors();

  auto chunk = pipe.Get();
  ASSERT_NO_THROW(std::ignore = moved_vm->Evaluate(chunk));
  EXPECT_EQ(errBuf.str(), "");
  EXPECT_EQ(outBuf.str(), "321\n");
}

TEST_F(CompilerTest, ImportErrorsShouldNotEscapeVM) {
  auto res = Run(R"(
try{ import bad; }
catch(e){ print("ok"); }
)");
  EXPECT_EQ(res, "ok\n");
}

TEST_F(CompilerTest, TryCatchThrow) {
  {
    auto res = Run(R"(
try{
  throw 5;
} catch(e){
  print(e);
}
)");
    EXPECT_EQ(res, "5\n");
  }

  {
    auto res = Run(R"(
result = 0;
try{
  result = 1;
  try{ throw 2; }
  catch(e){ result = e; }
} catch(e) {
  result = 3;
}

print(result);
)");
    EXPECT_EQ(res, "2\n");
  }

  {
    auto res = Run(R"(
try{
  a = 1;
  spawn a();
} catch(e){
  print(e);
}
)");
    EXPECT_EQ(res, "'<int: 1>' object is not callable.");
  }
}

TEST_F(CompilerTest, AsyncErrorPropagation) {
  {
    auto res = Run(R"(
fn nilAfter(t){ return await async.Sleep(t); }
fn errAfter(t){ await async.Sleep(t); throw t; }
first = spawn nilAfter(10);
second = spawn nilAfter(5);
thrid = spawn errAfter(5);
)");  // main fiber ends immediately, last fiber returns nil after 10ms
    // vm must report the fiber that throws
    EXPECT_EQ(res, error("5"));
  }

  {
    auto res = Run(R"(
fn errAfter(t){ await async.Sleep(t); throw t; }
fn foo(){
  return await async.Race(async.Sleep(5, "ok"), spawn errAfter(20));
}
print(foo());
)");
    EXPECT_EQ(res, "ok");
  }

  {
    auto res = Run(R"(
fn errAfter(t){ await async.Sleep(t); throw t; }
try{
  f = spawn errAfter(15);
  await f;
} catch(e) { print("first:", e); }
try{ await f; }
catch(e){ print("second:", e); }
)");
    EXPECT_EQ(res, "first: 15\nsecond: 15");
  }

  {
    auto res = Run(R"(
fn foo(){
  yield "first, yield value";
  throw "error on resume";
}
f = spawn foo();
await async.Sleep(1);
kFirstYieldValue = await f;
)");  // first await should push foo through yield statement and make it throw
    EXPECT_EQ(res, error("error on resume"));
  }
}

TEST_F(CompilerTest, InterpretPrint) {
  {
    auto res = Interpret({"fn foo(x){ if(x==1) return nil; }", "foo(0);",
                          "print(foo(0));", "foo(1);", "print(foo(1));"});
    EXPECT_EQ(res, "nil\nnil\n");
  }

  {
    auto res = Interpret({"1;", "nil;"});
    EXPECT_EQ(res, "1\n");
  }

  {
    auto res = Interpret(
        {"fn boo(){}", "fn foo(){ boo(); x=0; return x+1;}", "foo();"});
    EXPECT_EQ(res, "1\n");
  }

  {
    auto res = Interpret({
        R"(
for(i=0;i<3;i+=1){
  i;
  print(i);
}
)"});
    EXPECT_EQ(res, "0\n1\n2\n");
  }

  {
    auto res = Interpret({R"( if (true) 42; else print("43"); )"});
    EXPECT_EQ(res, "");
  }
}

TEST_F(CompilerTest, InterpretPreservesGlobalsAcrossChunks) {
  auto res = Interpret({"a = 123;", "a;"});
  EXPECT_EQ(res, "123\n");
}

// ==============================================================================
// Unhandled errors in repl mode
TEST_F(CompilerTest, UnhandledThrow) {
  {
    auto res = Interpret({"a=non_exist;", "\"still alive\";"});
    EXPECT_EQ(res, "NameError: 'non_exist' is not defined\nstill alive\n");
  }
}

TEST_F(CompilerTest, ListIndexType) {
  auto res = Interpret({
      R"(a = [1,2,3];)",
      R"(a["0"];)",
      R"("ok";)",
  });
  EXPECT_EQ(res, "list index must be integer, but got: <str: 0>\nok");
}

TEST_F(CompilerTest, DictIndexType) {
  auto res = Interpret({
      R"(a = {"x":1};)",
      R"(a[0];)",
      R"("ok";)",
  });
  EXPECT_EQ(res, "dictionary has no key: 0\nok");
}

TEST_F(CompilerTest, NegativeShiftRHS) {
  auto res = Interpret({
      R"(1 << -1;)",
      R"(1 >> -2;)",
      R"(1 >>> -3;)",
      R"("ok";)",
  });
  EXPECT_EQ(res, R"(
negative shift count: -1
negative shift count: -2
negative shift count: -3
ok
)");
}

TEST_F(CompilerTest, ItemAccessNonExist) {
  auto res = Interpret({
      R"(a = [1,2]; a[5];)",      // out of range
      R"(d = {"x":1}; d["y"];)",  // missing key
      R"("ok";)",
  });
  EXPECT_EQ(res, R"(
list index '5' out of range
dictionary has no key: y
ok
)");
}

TEST_F(CompilerTest, AssignmentNotSupported) {
  auto res = Interpret({
      R"(s = "abc"; s[0] = "x";)",
      R"("ok";)",
  });
  EXPECT_EQ(res, R"(
'<str: abc>' object does not support item assignment.
ok
)");
}

TEST_F(CompilerTest, NonHashableKey) {
  auto res = Interpret({
      R"( d = {}; k = []; )",
      R"( d[k] = 123; )",
      R"( d; )",
      R"( k; )",
  });
  EXPECT_EQ(res, R"(
'<list[0]>' object is not hashable.
{}
[]
)");
}

TEST_F(CompilerTest, ReturnOutside) {
  auto res = Interpret({R"( return print("abc"); )"});
  EXPECT_EQ(res, error("'return' outside function"));
}

TEST_F(CompilerTest, YieldOutside) {
  auto res = Interpret({R"( yield 1; )"});
  EXPECT_EQ(res, error("'yield' outside function"));
}

TEST_F(CompilerTest, AwaitNonAwaitable) {
  auto res = Interpret({R"( a = 1; b = "abc"; c = [1,2,3]; )", " await a; ",
                        " await b; ", " await c; "});

  EXPECT_EQ(res, R"(
object is not awaitable: <int: 1>
object is not awaitable: <str: abc>
object is not awaitable: <list[3]>
)");
}

}  // namespace m6test
